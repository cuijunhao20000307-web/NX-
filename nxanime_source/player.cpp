#include "player.hpp"

#include <SDL.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static constexpr int PLAYER_W = 1280;
static constexpr int PLAYER_H = 720;

static uint64_t now_ns() {
    return armTicksToNs(armGetSystemTick());
}

static std::string fferr(int code) {
    char b[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, b, sizeof(b));
    return b;
}

static void clear_framebuffer(Framebuffer* fb, uint32_t color = 0xFF000000u) {
    u32 stride = 0;
    auto* out = static_cast<u32*>(framebufferBegin(fb, &stride));
    if (!out) return;
    u32 sp = stride / 4;
    for (int y = 0; y < PLAYER_H; ++y) {
        u32* row = out + y * sp;
        for (int x = 0; x < PLAYER_W; ++x) row[x] = color;
    }
    framebufferEnd(fb);
}

static bool open_decoder(AVFormatContext* fmt, AVMediaType type,
                         int& stream_index, AVCodecContext*& ctx,
                         std::string& status) {
    const AVCodec* codec = nullptr;
    stream_index = av_find_best_stream(fmt, type, -1, -1, &codec, 0);
    if (stream_index < 0 || !codec) return false;
    ctx = avcodec_alloc_context3(codec);
    if (!ctx) { status = "无法分配解码器"; return false; }
    int rc = avcodec_parameters_to_context(ctx, fmt->streams[stream_index]->codecpar);
    if (rc < 0) { status = "读取媒体参数失败：" + fferr(rc); return false; }
    ctx->thread_count = 4;
    ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    rc = avcodec_open2(ctx, codec, nullptr);
    if (rc < 0) { status = "打开解码器失败：" + fferr(rc); return false; }
    return true;
}

static std::string http_proxy_url(const ProxyConfig& p) {
    if (p.mode != PROXY_MODE_HTTP || !p.host[0] || p.port <= 0) return {};
    char b[768];
    if (p.username[0])
        std::snprintf(b, sizeof(b), "http://%s:%s@%s:%d", p.username, p.password, p.host, p.port);
    else
        std::snprintf(b, sizeof(b), "http://%s:%d", p.host, p.port);
    return b;
}

PlayerResult player_play(Framebuffer* framebuffer,
                         PadState* pad,
                         const ProxyConfig& proxy,
                         const MediaSource& source,
                         const std::string& title,
                         std::string& status) {
    (void)title;
    if (!framebuffer || !pad || source.url.empty()) {
        status = "播放器参数无效";
        return PLAYER_ERROR;
    }
    if (proxy.mode == PROXY_MODE_SOCKS5) {
        status = "原生播放器暂不支持 SOCKS5 代理，请改用直连或 HTTP 代理";
        return PLAYER_ERROR;
    }

    clear_framebuffer(framebuffer);
    avformat_network_init();

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "user_agent",
                "Mozilla/5.0 (Nintendo Switch; NXAnime) AppleWebKit/605.1.15 Version/17.0 Safari/605.1.15", 0);
    av_dict_set(&opts, "rw_timeout", "15000000", 0);
    av_dict_set(&opts, "reconnect", "1", 0);
    av_dict_set(&opts, "reconnect_streamed", "1", 0);
    av_dict_set(&opts, "reconnect_delay_max", "3", 0);
    std::string headers;
    if (!source.referer.empty()) {
        headers = "Referer: " + source.referer + "\r\nOrigin: https://ani.girigirilove.com\r\n";
        av_dict_set(&opts, "headers", headers.c_str(), 0);
    }
    std::string hp = http_proxy_url(proxy);
    if (!hp.empty()) av_dict_set(&opts, "http_proxy", hp.c_str(), 0);

    AVFormatContext* fmt = nullptr;
    int rc = avformat_open_input(&fmt, source.url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        status = "打开视频失败：" + fferr(rc);
        return PLAYER_ERROR;
    }
    rc = avformat_find_stream_info(fmt, nullptr);
    if (rc < 0) {
        status = "读取视频信息失败：" + fferr(rc);
        avformat_close_input(&fmt);
        return PLAYER_ERROR;
    }

    int vsi = -1, asi = -1;
    AVCodecContext* vctx = nullptr;
    AVCodecContext* actx = nullptr;
    if (!open_decoder(fmt, AVMEDIA_TYPE_VIDEO, vsi, vctx, status)) {
        if (vctx) avcodec_free_context(&vctx);
        avformat_close_input(&fmt);
        if (status.empty()) status = "播放源没有可用视频轨";
        return PLAYER_ERROR;
    }
    std::string audio_status;
    bool have_audio = open_decoder(fmt, AVMEDIA_TYPE_AUDIO, asi, actx, audio_status);

    SDL_AudioDeviceID audio_dev = 0;
    bool sdl_audio = false;
    if (have_audio && SDL_Init(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want{};
        want.freq = 48000;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 2048;
        audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
        if (audio_dev) {
            sdl_audio = true;
            SDL_PauseAudioDevice(audio_dev, 0);
        }
    }

    SwrContext* swr = nullptr;
    if (sdl_audio && actx) {
        AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
        rc = swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_S16, 48000,
                                 &actx->ch_layout, actx->sample_fmt, actx->sample_rate,
                                 0, nullptr);
        if (rc < 0 || !swr || swr_init(swr) < 0) {
            if (swr) swr_free(&swr);
            sdl_audio = false;
            if (audio_dev) { SDL_CloseAudioDevice(audio_dev); audio_dev = 0; }
        }
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    SwsContext* sws = nullptr;
    std::vector<uint8_t> rgba;
    int scaled_w = 0, scaled_h = 0;

    bool paused = false;
    bool clock_set = false;
    double clock_pts = 0.0;
    uint64_t clock_ns = 0;
    double last_video_pts = 0.0;
    PlayerResult result = PLAYER_ENDED;

    auto cleanup = [&]() {
        if (sdl_audio && audio_dev) {
            SDL_ClearQueuedAudio(audio_dev);
            SDL_CloseAudioDevice(audio_dev);
        }
        if (have_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        if (swr) swr_free(&swr);
        if (sws) sws_freeContext(sws);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (actx) avcodec_free_context(&actx);
        if (vctx) avcodec_free_context(&vctx);
        if (fmt) avformat_close_input(&fmt);
        avformat_network_deinit();
    };

    auto handle_buttons = [&](bool allow_pause)->int {
        padUpdate(pad);
        u64 d = padGetButtonsDown(pad);
        if (d & (HidNpadButton_B | HidNpadButton_Plus)) return -1000;
        if (allow_pause && (d & HidNpadButton_A)) return 1000;
        if (d & (HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_StickRLeft)) return -10;
        if (d & (HidNpadButton_Right | HidNpadButton_StickLRight | HidNpadButton_StickRRight)) return 10;
        return 0;
    };

    auto do_seek = [&](double target) {
        if (target < 0.0) target = 0.0;
        int64_t ts = (int64_t)(target * AV_TIME_BASE);
        if (av_seek_frame(fmt, -1, ts, AVSEEK_FLAG_BACKWARD) >= 0) {
            avcodec_flush_buffers(vctx);
            if (actx) avcodec_flush_buffers(actx);
            if (sdl_audio && audio_dev) SDL_ClearQueuedAudio(audio_dev);
            if (swr) { swr_close(swr); swr_init(swr); }
            clock_set = false;
            last_video_pts = target;
        }
    };

    auto render_video = [&](AVFrame* vf) {
        int sw = vf->width, sh = vf->height;
        if (sw <= 0 || sh <= 0) return;
        double scale = std::min((double)PLAYER_W / sw, (double)PLAYER_H / sh);
        int tw = std::max(2, (int)(sw * scale));
        int th = std::max(2, (int)(sh * scale));
        tw &= ~1; th &= ~1;
        sws = sws_getCachedContext(sws, sw, sh, (AVPixelFormat)vf->format,
                                   tw, th, AV_PIX_FMT_RGBA, SWS_BILINEAR,
                                   nullptr, nullptr, nullptr);
        if (!sws) return;
        if (tw != scaled_w || th != scaled_h) {
            scaled_w = tw; scaled_h = th;
            rgba.resize((size_t)tw * th * 4);
        }
        uint8_t* dst_data[4] = { rgba.data(), nullptr, nullptr, nullptr };
        int dst_lines[4] = { tw * 4, 0, 0, 0 };
        sws_scale(sws, vf->data, vf->linesize, 0, sh, dst_data, dst_lines);

        u32 stride = 0;
        auto* out = static_cast<u32*>(framebufferBegin(framebuffer, &stride));
        if (!out) return;
        u32 sp = stride / 4;
        for (int y = 0; y < PLAYER_H; ++y) {
            u32* row = out + y * sp;
            for (int x = 0; x < PLAYER_W; ++x) row[x] = 0xFF000000u;
        }
        int ox = (PLAYER_W - tw) / 2;
        int oy = (PLAYER_H - th) / 2;
        for (int y = 0; y < th; ++y) {
            std::memcpy(reinterpret_cast<uint8_t*>(out + (oy+y)*sp + ox),
                        rgba.data() + (size_t)y * tw * 4, (size_t)tw * 4);
        }
        framebufferEnd(framebuffer);
    };

    while (appletMainLoop()) {
        int cmd = handle_buttons(true);
        if (cmd == -1000) { result = PLAYER_BACK; status = "已退出播放"; break; }
        if (cmd == 1000) {
            paused = !paused;
            if (sdl_audio && audio_dev) SDL_PauseAudioDevice(audio_dev, paused ? 1 : 0);
            uint64_t pause_begin = now_ns();
            while (paused && appletMainLoop()) {
                int pcmd = handle_buttons(true);
                if (pcmd == -1000) { result = PLAYER_BACK; status = "已退出播放"; paused = false; break; }
                if (pcmd == 1000) {
                    paused = false;
                    if (sdl_audio && audio_dev) SDL_PauseAudioDevice(audio_dev, 0);
                    if (clock_set) clock_ns += now_ns() - pause_begin;
                    break;
                }
                if (pcmd == -10 || pcmd == 10) {
                    do_seek(last_video_pts + pcmd);
                    pause_begin = now_ns();
                }
                svcSleepThread(16'000'000);
            }
            if (result == PLAYER_BACK) break;
        } else if (cmd == -10 || cmd == 10) {
            do_seek(last_video_pts + cmd);
        }

        rc = av_read_frame(fmt, pkt);
        if (rc == AVERROR_EOF) {
            status = "播放完成";
            result = PLAYER_ENDED;
            break;
        }
        if (rc < 0) {
            status = "读取播放流失败：" + fferr(rc);
            result = PLAYER_ERROR;
            break;
        }

        AVCodecContext* dec = nullptr;
        bool is_video = false;
        if (pkt->stream_index == vsi) { dec = vctx; is_video = true; }
        else if (have_audio && pkt->stream_index == asi) dec = actx;
        if (!dec) { av_packet_unref(pkt); continue; }

        rc = avcodec_send_packet(dec, pkt);
        av_packet_unref(pkt);
        if (rc < 0 && rc != AVERROR(EAGAIN)) continue;
        while (avcodec_receive_frame(dec, frame) == 0) {
            if (is_video) {
                int64_t pts_i = frame->best_effort_timestamp;
                double pts = pts_i == AV_NOPTS_VALUE ? last_video_pts : pts_i * av_q2d(fmt->streams[vsi]->time_base);
                if (!clock_set) {
                    clock_set = true;
                    clock_pts = pts;
                    clock_ns = now_ns();
                }
                last_video_pts = pts;
                double delta = pts - clock_pts;
                if (delta >= 0.0 && delta < 10.0) {
                    uint64_t target = clock_ns + (uint64_t)(delta * 1000000000.0);
                    uint64_t n = now_ns();
                    if (target > n && target - n < 500000000ULL)
                        svcSleepThread((int64_t)(target - n));
                }
                render_video(frame);
            } else if (sdl_audio && swr && audio_dev) {
                int in_rate = actx->sample_rate > 0 ? actx->sample_rate : 48000;
                int out_samples = (int)av_rescale_rnd(swr_get_delay(swr, in_rate) + frame->nb_samples,
                                                       48000, in_rate, AV_ROUND_UP);
                int max_bytes = av_samples_get_buffer_size(nullptr, 2, out_samples, AV_SAMPLE_FMT_S16, 1);
                if (max_bytes > 0) {
                    std::vector<uint8_t> audio((size_t)max_bytes);
                    uint8_t* dst[1] = { audio.data() };
                    int got = swr_convert(swr, dst, out_samples,
                                          (const uint8_t**)frame->extended_data, frame->nb_samples);
                    if (got > 0) {
                        int bytes = got * 2 * (int)sizeof(int16_t);
                        while (SDL_GetQueuedAudioSize(audio_dev) > 48000 * 2 * sizeof(int16_t) && appletMainLoop())
                            svcSleepThread(8'000'000);
                        SDL_QueueAudio(audio_dev, audio.data(), (Uint32)bytes);
                    }
                }
            }
            av_frame_unref(frame);
        }
    }

    cleanup();
    clear_framebuffer(framebuffer);
    return result;
}
