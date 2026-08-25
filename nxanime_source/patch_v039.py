from pathlib import Path

main = Path('nxanime_source/main.cpp')
m = main.read_text(encoding='utf-8')

# Replace the ASCII-only visible startup page with a Chinese HOS-font UI.
# If HOS shared-font initialization fails, keep the old ASCII fallback instead of exiting.
start = m.find('static void draw_safe(u32* b,u32 s,const State& st){')
end = m.find('\n\nstatic void draw_cover_panel', start)
if start < 0 or end < 0:
    raise SystemExit('draw_safe block not found')

new_safe = r'''static const char* proxy_mode_cn(ProxyMode mode){
    switch(mode){
        case PROXY_MODE_HTTP: return "HTTP 代理";
        case PROXY_MODE_SOCKS5: return "SOCKS5 代理";
        default: return "直连";
    }
}

static void draw_safe(u32* b,u32 s,const State& st){
    u32 bg=rgba(9,10,14),panel=rgba(20,22,30),white=rgba(244,245,248),muted=rgba(150,156,170),pink=rgba(255,62,126),green=rgba(55,205,120);
    rect(b,s,0,0,W,H,bg);rect(b,s,0,0,W,92,panel);rect(b,s,0,90,W,2,pink);

    if(g_font_ok){
        text(b,42,66,38,"NXAnime",white,280);
        text(b,250,63,18,"启动页 · v0.3.9",pink,280);
        text(b,42,150,30,"GiriGiri 数据源",white,420);
        text(b,42,184,16,"安全启动 · HOS 系统中文字体 · 网络按需连接",muted,780);

        rect(b,s,42,220,1196,90,panel);
        border(b,s,42,220,1196,90,2,st.connected?green:rgba(65,70,82));
        text(b,68,260,23,st.connected?"数据源已连接":"数据源未连接",st.connected?green:muted,420);
        text(b,68,292,16,"目录数量："+std::to_string(st.items.size()),white,360);

        rect(b,s,42,335,1196,180,panel);
        text(b,68,390,24,"状态",pink,180);
        std::string status_cn=st.status;
        if(status_cn=="READY - PRESS A TO CONNECT")status_cn="就绪 · 按 A 连接";
        else if(status_cn=="GiriGiri 连接正常")status_cn="GiriGiri 连接正常";
        text(b,68,435,19,status_cn,white,1080,2);
        text(b,68,486,17,std::string("代理模式：")+proxy_mode_cn(st.proxy.mode),muted,520);

        rect(b,s,42,540,1196,62,panel);
        text(b,68,580,18,"A 连接",white,180);
        text(b,330,580,18,"ZR 代理",muted,180);
        text(b,650,580,18,"Y 测试",muted,180);
        text(b,1080,580,18,"+ 退出",white,140);
        return;
    }

    // Last-resort fallback: keep startup usable even if HOS shared fonts are unavailable.
    safe_text(b,s,42,28,6,"NXANIME",white);
    safe_text(b,s,330,34,3,"SAFE BOOT 0.3.9",pink);
    safe_text(b,s,42,128,4,"GIRIGIRI PROVIDER",white);
    safe_text(b,s,42,174,2,"HOS FONT FAILED - ENGLISH FALLBACK",muted);
    rect(b,s,42,220,1196,90,panel);border(b,s,42,220,1196,90,2,st.connected?green:rgba(65,70,82));
    safe_text(b,s,68,245,3,st.connected?"SOURCE CONNECTED":"SOURCE NOT CONNECTED",st.connected?green:muted);
    char c[80];snprintf(c,sizeof(c),"CATALOG ITEMS: %zu",st.items.size());safe_text(b,s,68,280,2,c,white);
    rect(b,s,42,335,1196,180,panel);safe_text(b,s,68,362,3,"STATUS",pink);
    safe_text(b,s,68,405,2,"READY - PRESS A TO CONNECT",white);
    char pm[160];snprintf(pm,sizeof(pm),"PROXY MODE: %s",proxy_mode_name(st.proxy.mode));safe_text(b,s,68,455,2,pm,muted);
    rect(b,s,42,540,1196,62,panel);safe_text(b,s,68,558,2,"A CONNECT",white);safe_text(b,s,330,558,2,"ZR PROXY",muted);safe_text(b,s,650,558,2,"Y TEST",muted);safe_text(b,s,1080,558,2,"+ EXIT",white);
}'''

m = m[:start] + new_safe + m[end:]

# Initialize HOS shared fonts before the first visible frame, but never make font failure fatal.
old_startup = r'''    Framebuffer fb;Result frc=framebufferCreate(&fb,nwindowGetDefault(),W,H,PIXEL_FORMAT_RGBA_8888,2);if(R_FAILED(frc))return (int)frc;framebufferMakeLinear(&fb);
    State st;proxy_config_load(&st.proxy);draw_frame(fb,st);'''
new_startup = r'''    Framebuffer fb;Result frc=framebufferCreate(&fb,nwindowGetDefault(),W,H,PIXEL_FORMAT_RGBA_8888,2);if(R_FAILED(frc))return (int)frc;framebufferMakeLinear(&fb);
    State st;proxy_config_load(&st.proxy);
    std::string startup_font_error;
    if(!init_hos_fonts(startup_font_error)){
        log_stage((std::string("startup HOS font fallback: ")+startup_font_error).c_str());
    }
    draw_frame(fb,st);'''
if old_startup not in m:
    raise SystemExit('startup framebuffer block not found')
m = m.replace(old_startup, new_startup, 1)

# Keep the later connect-time font call harmless: init_hos_fonts() returns immediately if already ready.
m = m.replace('page+" · v0.3.8"', 'page+" · v0.3.9"')
m = m.replace('NXAnime 0.3.8 startup', 'NXAnime 0.3.9 startup')

main.write_text(m, encoding='utf-8')
print('v0.3.9 Chinese startup UI patch applied')
