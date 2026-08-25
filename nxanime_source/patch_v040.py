from pathlib import Path
import re

main = Path('nxanime_source/main.cpp')
m = main.read_text(encoding='utf-8')

# Player modules.
inc = '#include "provider.hpp"\n'
if inc not in m:
    raise SystemExit('provider include not found')
m = m.replace(inc, inc + '#include "media_resolver.hpp"\n#include "player.hpp"\n', 1)

# Replace the placeholder episode page with the native-player status/retry page.
start = m.find('static void draw_episode(u32* b,const State& st){')
end = m.find('\nstatic void draw_frame(', start)
if start < 0 or end < 0:
    raise SystemExit('draw_episode block not found')
new_episode = r'''static void draw_episode(u32* b,const State& st){
    header(b,"播放");
    u32 white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126),card=rgba(24,27,37),edge=rgba(61,66,82),green=rgba(55,205,120);
    std::string label="集数";
    if(!st.detail.episodes.empty()&&st.ep<(int)st.detail.episodes.size())label=st.detail.episodes[st.ep].label;
    text(b,40,145,29,st.detail.title+" · "+label,white,1160,1);
    rect(b,g_stride,40,190,1200,330,card);border(b,g_stride,40,190,1200,330,1,edge);
    text(b,70,245,24,"NXAnime 原生播放器",pink,500);
    text(b,70,292,17,"支持页面直接公开的 MP4 / HLS(m3u8) · FFmpeg 解码",muted,1100,1);
    text(b,70,352,19,"状态",white,120);
    bool unsupported=st.status.find("暂不支持")!=std::string::npos||st.status.find("没有直接公开")!=std::string::npos;
    text(b,70,394,18,st.status.empty()?"按 A 开始解析播放源":st.status,unsupported?pink:green,1090,3);
    rect(b,g_stride,70,455,320,48,rgba(30,33,44));border(b,g_stride,70,455,320,48,2,pink);
    text(b,102,488,18,"A 开始播放 / 重试",white,250);
    text(b,430,488,16,"播放中：A 暂停  ←/→ 快退快进  B 返回",muted,740,1);
    footer(b,st,"A 播放   B 返回详情");
}'''
m = m[:start] + new_episode + m[end:]

# Insert playback orchestration after all catalog/detail/network helpers and before touch helpers.
marker = '\nstatic bool hit_box(u32 x,u32 y,int bx,int by,int bw,int bh){'
if marker not in m:
    raise SystemExit('hit_box marker not found')
play_helper = r'''
static void play_current_episode(State& st,Framebuffer& fb,PadState& pad){
    if(st.detail.episodes.empty()||st.ep<0||st.ep>=(int)st.detail.episodes.size()){
        st.status="没有可播放的集数";st.screen=EPISODE;return;
    }
    while(st.ep<(int)st.detail.episodes.size()){
        const EpisodeItem epitem=st.detail.episodes[st.ep];
        st.screen=EPISODE;
        st.status="正在解析公开播放源...";
        draw_frame(fb,st);
        MediaSource src;std::string msg;
        if(!media_resolve_public(st.proxy,epitem,src,msg)){
            st.status=msg;st.screen=EPISODE;draw_frame(fb,st);return;
        }
        st.status=msg+" · 正在打开播放器...";
        draw_frame(fb,st);
        std::string play_status;
        PlayerResult r=player_play(&fb,&pad,st.proxy,src,st.detail.title+" · "+epitem.label,play_status);
        st.status=play_status;
        if(r==PLAYER_BACK){st.screen=DETAIL;return;}
        if(r==PLAYER_ERROR){st.screen=EPISODE;return;}
        if(r==PLAYER_ENDED){
            if(st.ep+1<(int)st.detail.episodes.size()){
                ++st.ep;
                st.status="本集播放完成 · 自动播放下一集";
                draw_frame(fb,st);
                continue;
            }
            st.status="播放完成 · 已到最后一集";
            st.screen=DETAIL;
            return;
        }
    }
}
'''
m = m.replace(marker, play_helper + marker, 1)

# Let touch handler launch playback as well.
old_sig = 'static void handle_touch(State& st,Framebuffer& fb,u32 tx,u32 ty){'
new_sig = 'static void handle_touch(State& st,Framebuffer& fb,PadState& pad,u32 tx,u32 ty){'
if old_sig not in m:
    raise SystemExit('handle_touch signature not found')
m = m.replace(old_sig, new_sig, 1)
m = m.replace('if(same)st.screen=EPISODE;', 'if(same)play_current_episode(st,fb,pad);', 1)

# Episode-page touch: central card retries playback; footer/left remains back.
old_ep_touch = r'''    if(st.screen==EPISODE){
        if(ty>=620||tx<180)st.screen=DETAIL;
    }
'''
new_ep_touch = r'''    if(st.screen==EPISODE){
        if(hit_box(tx,ty,40,190,1200,370)){play_current_episode(st,fb,pad);return;}
        if(ty>=620||tx<180)st.screen=DETAIL;
    }
'''
if old_ep_touch in m:
    m = m.replace(old_ep_touch, new_ep_touch, 1)

old_touch_call = 'if(touch_pressed)handle_touch(st,fb,tx,ty);'
if old_touch_call not in m:
    raise SystemExit('touch call not found')
m = m.replace(old_touch_call, 'if(touch_pressed)handle_touch(st,fb,pad,tx,ty);', 1)

# Controller: A on selected episode goes directly into native playback.
old_detail_a = 'if(d&HidNpadButton_A)st.screen=EPISODE;'
if old_detail_a not in m:
    raise SystemExit('detail A action not found')
m = m.replace(old_detail_a, 'if(d&HidNpadButton_A)play_current_episode(st,fb,pad);', 1)

old_ep_branch = '}else if(st.screen==EPISODE){if(d&HidNpadButton_B)st.screen=DETAIL;}'
if old_ep_branch not in m:
    raise SystemExit('episode input branch not found')
new_ep_branch = '''}else if(st.screen==EPISODE){
            if(d&HidNpadButton_A)play_current_episode(st,fb,pad);
            if(d&HidNpadButton_B)st.screen=DETAIL;
        }'''
m = m.replace(old_ep_branch, new_ep_branch, 1)

# Final feature version label.
m = m.replace('v0.3.9', 'v0.4.0')
m = m.replace('NXAnime 0.3.9 startup', 'NXAnime 0.4.0 startup')
m = m.replace('SAFE BOOT 0.3.9', 'SAFE BOOT 0.4.0')

main.write_text(m, encoding='utf-8')
print('v0.4.0 native player patch applied')
