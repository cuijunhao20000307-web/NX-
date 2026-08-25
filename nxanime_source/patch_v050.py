from pathlib import Path

MOBILE_UA = 'Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Mobile Safari/537.36'

player = Path('nxanime_source/player.cpp')
player.write_text(r'''#include "player.hpp"

#include <mpv/client.h>
#include <mpv/render.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static constexpr int PLAYER_W = 1280;
static constexpr int PLAYER_H = 720;
static constexpr const char* MOBILE_UA = "Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Mobile Safari/537.36";

static void clear_fb(Framebuffer* fb, u32 color = 0xFF000000u) {
    u32 stride = 0;
    auto* out = static_cast<u32*>(framebufferBegin(fb, &stride));
    if (!out) return;
    const u32 sp = stride / 4;
    for (int y = 0; y < PLAYER_H; ++y) {
        u32* row = out + y * sp;
        for (int x = 0; x < PLAYER_W; ++x) row[x] = color;
    }
    framebufferEnd(fb);
}

static void present_rgba(Framebuffer* fb, const std::vector<unsigned char>& rgba) {
    if (rgba.size() < (size_t)PLAYER_W * PLAYER_H * 4) return;
    u32 stride = 0;
    auto* out = static_cast<u32*>(framebufferBegin(fb, &stride));
    if (!out) return;
    const size_t row_bytes = (size_t)PLAYER_W * 4;
    const u32 sp = stride / 4;
    for (int y = 0; y < PLAYER_H; ++y) {
        std::memcpy(reinterpret_cast<unsigned char*>(out + y * sp),
                    rgba.data() + (size_t)y * row_bytes, row_bytes);
    }
    framebufferEnd(fb);
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

static bool mpv_set_str(mpv_handle* mpv, const char* name, const char* value) {
    return mpv_set_option_string(mpv, name, value) >= 0;
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
        status = "libmpv 播放器暂不支持当前 SOCKS5 配置，请使用直连或 HTTP 代理";
        return PLAYER_ERROR;
    }

    clear_fb(framebuffer);

    mpv_handle* mpv = mpv_create();
    if (!mpv) {
        status = "无法创建 libmpv 播放器";
        return PLAYER_ERROR;
    }

    // Same basic Switch-oriented settings used by wiliwili's MPVCore, trimmed for NXAnime.
    mpv_set_str(mpv, "config", "no");
    mpv_set_str(mpv, "terminal", "no");
    mpv_set_str(mpv, "ytdl", "no");
    mpv_set_str(mpv, "audio-channels", "stereo");
    mpv_set_str(mpv, "idle", "yes");
    mpv_set_str(mpv, "loop-file", "no");
    mpv_set_str(mpv, "osd-level", "0");
    mpv_set_str(mpv, "keep-open", "no");
    mpv_set_str(mpv, "hr-seek", "yes");
    mpv_set_str(mpv, "vo", "libmpv");
    mpv_set_str(mpv, "hwdec", "no");
    mpv_set_str(mpv, "vd-lavc-dr", "no");
    mpv_set_str(mpv, "vd-lavc-threads", "4");
    mpv_set_str(mpv, "demuxer-lavf-analyzeduration", "0.4");
    mpv_set_str(mpv, "demuxer-lavf-probescore", "24");
    mpv_set_str(mpv, "user-agent", MOBILE_UA);

    if (!source.referer.empty()) {
        std::string fields = "Referer: " + source.referer + ",Origin: https://ani.girigirilove.com";
        mpv_set_str(mpv, "http-header-fields", fields.c_str());
    }
    const std::string proxy_url = http_proxy_url(proxy);
    if (!proxy_url.empty()) mpv_set_str(mpv, "http-proxy", proxy_url.c_str());

    int rc = mpv_initialize(mpv);
    if (rc < 0) {
        status = std::string("libmpv 初始化失败：") + mpv_error_string(rc);
        mpv_terminate_destroy(mpv);
        return PLAYER_ERROR;
    }

    mpv_render_context* render = nullptr;
    mpv_render_param init_params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_SW)},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    rc = mpv_render_context_create(&render, mpv, init_params);
    if (rc < 0 || !render) {
        status = std::string("libmpv 软件渲染初始化失败：") + mpv_error_string(rc);
        mpv_terminate_destroy(mpv);
        return PLAYER_ERROR;
    }

    int sw_size[2] = {PLAYER_W, PLAYER_H};
    const char* sw_format = "rgba";
    size_t pitch = (size_t)PLAYER_W * 4;
    std::vector<unsigned char> pixels((size_t)PLAYER_W * PLAYER_H * 4, 0);
    mpv_render_param render_params[] = {
        {MPV_RENDER_PARAM_SW_SIZE, sw_size},
        {MPV_RENDER_PARAM_SW_FORMAT, const_cast<char*>(sw_format)},
        {MPV_RENDER_PARAM_SW_STRIDE, &pitch},
        {MPV_RENDER_PARAM_SW_POINTER, pixels.data()},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    const char* load_cmd[] = {"loadfile", source.url.c_str(), "replace", nullptr};
    rc = mpv_command(mpv, load_cmd);
    if (rc < 0) {
        status = std::string("libmpv 无法载入视频：") + mpv_error_string(rc);
        mpv_render_context_free(render);
        mpv_terminate_destroy(mpv);
        return PLAYER_ERROR;
    }

    bool paused = false;
    bool loaded = false;
    PlayerResult result = PLAYER_ERROR;
    status = "libmpv 正在缓冲...";

    while (appletMainLoop()) {
        padUpdate(pad);
        const u64 d = padGetButtonsDown(pad);
        if (d & (HidNpadButton_B | HidNpadButton_Plus)) {
            result = PLAYER_BACK;
            status = "已退出播放";
            break;
        }
        if (d & HidNpadButton_A) {
            paused = !paused;
            const char* value = paused ? "yes" : "no";
            mpv_set_property_string(mpv, "pause", value);
        }
        if (d & (HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_StickRLeft)) {
            const char* cmd[] = {"seek", "-10", "relative", nullptr};
            mpv_command(mpv, cmd);
        }
        if (d & (HidNpadButton_Right | HidNpadButton_StickLRight | HidNpadButton_StickRRight)) {
            const char* cmd[] = {"seek", "10", "relative", nullptr};
            mpv_command(mpv, cmd);
        }

        while (true) {
            mpv_event* ev = mpv_wait_event(mpv, 0.0);
            if (!ev || ev->event_id == MPV_EVENT_NONE) break;
            if (ev->event_id == MPV_EVENT_FILE_LOADED) {
                loaded = true;
                status = "正在播放";
            } else if (ev->event_id == MPV_EVENT_END_FILE) {
                auto* end = static_cast<mpv_event_end_file*>(ev->data);
                if (end && end->reason == MPV_END_FILE_REASON_EOF) {
                    status = "播放完成";
                    result = PLAYER_ENDED;
                } else {
                    int err = end ? end->error : MPV_ERROR_GENERIC;
                    status = std::string("libmpv 播放失败：") + mpv_error_string(err);
                    result = PLAYER_ERROR;
                }
                goto done;
            } else if (ev->event_id == MPV_EVENT_SHUTDOWN) {
                status = "libmpv 已关闭";
                result = PLAYER_ERROR;
                goto done;
            }
        }

        const uint64_t flags = mpv_render_context_update(render);
        if (flags & MPV_RENDER_UPDATE_FRAME) {
            if (mpv_render_context_render(render, render_params) >= 0) {
                present_rgba(framebuffer, pixels);
                mpv_render_context_report_swap(render);
            }
        } else if (!loaded) {
            svcSleepThread(8'000'000);
        } else {
            svcSleepThread(4'000'000);
        }
    }

done:
    mpv_render_context_free(render);
    mpv_terminate_destroy(mpv);
    clear_fb(framebuffer);
    return result;
}
''', encoding='utf-8')

# Link libmpv instead of the hand-written FFmpeg/SDL player dependencies.
mk = Path('Makefile')
s = mk.read_text(encoding='utf-8')
s = s.replace('APP_VERSION := 0.4.0', 'APP_VERSION := 0.5.0')
s = s.replace('freetype2 libcurl libpng libwebp sdl2 libavformat libavcodec libavutil libswscale libswresample',
              'freetype2 libcurl libpng libwebp mpv')
s = s.replace('libcurl freetype2 libpng libwebp sdl2 libavformat libavcodec libavutil libswscale libswresample',
              'libcurl freetype2 libpng libwebp mpv')
mk.write_text(s, encoding='utf-8')

# Remove the HOS WebApplet fallback. The player is now entirely in-process.
main = Path('nxanime_source/main.cpp')
m = main.read_text(encoding='utf-8')
start = m.find('            if(msg.find("encrypt:2")!=std::string::npos){')
if start >= 0:
    # Find the block ending right before the generic failure assignment.
    end_marker = '            st.status=msg;st.screen=EPISODE;draw_frame(fb,st);return;\n'
    end = m.find(end_marker, start)
    if end < 0:
        raise SystemExit('v0.4.1 web fallback end not found')
    replacement = '''            if(msg.find("encrypt:2")!=std::string::npos){\n                st.status="该集页面只提供站点编码播放信息 · libmpv 需要公开媒体 URL";\n                st.screen=EPISODE;draw_frame(fb,st);return;\n            }\n'''
    m = m[:start] + replacement + m[end:]

m = m.replace('v0.4.2', 'v0.5.0')
m = m.replace('NXAnime 0.4.2 startup', 'NXAnime 0.5.0 startup')
m = m.replace('SAFE BOOT 0.4.2', 'SAFE BOOT 0.5.0')
m = m.replace('NXAnime 原生播放器', 'NXAnime · libmpv 播放器')
m = m.replace('FFmpeg 解码', 'wiliwili 同类 libmpv 核心')
main.write_text(m, encoding='utf-8')

print('v0.5.0 wiliwili-style libmpv player patch applied')
