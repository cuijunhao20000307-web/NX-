from pathlib import Path

main = Path('nxanime_source/main.cpp')
m = main.read_text(encoding='utf-8')

old = '''        if(!media_resolve_public(st.proxy,epitem,src,msg)){
            st.status=msg;st.screen=EPISODE;draw_frame(fb,st);return;
        }
'''
new = r'''        if(!media_resolve_public(st.proxy,epitem,src,msg)){
            // GiriGiri currently returns encrypt:2 for many episodes. Do not decode or
            // extract that value here; hand the original public episode page to HOS WebApplet
            // so the site's own JavaScript/player can run in a real browser environment.
            if(msg.find("encrypt:2")!=std::string::npos){
                st.status="该源需要网页播放器 · 正在打开 HOS 浏览器...";
                st.screen=EPISODE;draw_frame(fb,st);

                WebCommonConfig webcfg{};
                Result wrc=webPageCreate(&webcfg,epitem.url.c_str());
                if(R_SUCCEEDED(wrc)){
                    // Allow HTTPS navigation/resources used by the site's player/CDN.
                    // The applet is still launched only for the selected public episode URL.
                    webConfigSetWhitelist(&webcfg,"^https://.*$");
                    webConfigSetFooter(&webcfg,true);
                    webConfigSetPointer(&webcfg,true);
                    webConfigSetLeftStickMode(&webcfg,WebLeftStickMode_Pointer);
                    webConfigSetDisplayUrlKind(&webcfg,false);
                    if(hosversionAtLeast(2,0,0))webConfigSetBootAsMediaPlayer(&webcfg,true);
                    WebCommonReply reply{};
                    wrc=webConfigShow(&webcfg,&reply);
                }

                if(R_FAILED(wrc)){
                    char tmp[160];
                    std::snprintf(tmp,sizeof(tmp),"HOS 浏览器启动失败：0x%08X · 请用 Application Mode 启动 NXAnime",wrc);
                    st.status=tmp;
                    st.screen=EPISODE;
                }else{
                    st.status="网页播放器已关闭 · A 可重新打开";
                    st.screen=EPISODE;
                }
                draw_frame(fb,st);
                return;
            }
            st.status=msg;st.screen=EPISODE;draw_frame(fb,st);return;
        }
'''
if old not in m:
    raise SystemExit('v0.4.0 media failure block not found')
m = m.replace(old,new,1)

m = m.replace('v0.4.0','v0.4.1')
m = m.replace('NXAnime 0.4.0 startup','NXAnime 0.4.1 startup')
m = m.replace('SAFE BOOT 0.4.0','SAFE BOOT 0.4.1')

main.write_text(m, encoding='utf-8')
print('v0.4.1 HOS WebApplet fallback patch applied')
