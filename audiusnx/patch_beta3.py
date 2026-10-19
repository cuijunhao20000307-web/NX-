#!/usr/bin/env python3
"""Reproducible Beta 3 patch to the isolated AudiusNX Beta 2 source archive."""
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('.')
p=root/'source/main.cpp'
s=p.read_text()
def swap(old,new):
    global s
    if old not in s: raise SystemExit('PATCH MISMATCH: '+old[:110])
    s=s.replace(old,new,1)
swap('#include <SDL_image.h>','#include <SDL_image.h>\n#include <SDL_mixer.h>')
swap('TTF_Font* fonts[4]{};', 'TTF_Font* fonts[4]{};\nTTF_Font* scriptFonts[4][5]{}; // Standard, Simplified, Traditional, Korean, Extended')
swap('bool connected=false, showPlayer=false, selectingSidebar=false, playing=false, plActive=false;', 'bool connected=false, showPlayer=false, selectingSidebar=false, playing=false, plActive=false;\nbool foregroundReady=false, localPlayback=false;\nMix_Music* localMusic=nullptr;')
start=s.index('void text(const std::string& value,')
end=s.index('\nvoid line(', start)
s=s[:start]+r'''// Switch shared fonts cover different Unicode ranges. One Chinese font is not enough:
// Korean/Hangul, Japanese, Latin accents and Chinese use different faces.
struct Run {std::string chars;TTF_Font* font;};
Uint32 nextUtf8(const std::string& s,size_t& pos){
    unsigned char a=(unsigned char)s[pos++];
    if(a<128)return a;
    int n=(a>=0xF0&&a<=0xF4)?3:(a>=0xE0&&a<=0xEF)?2:(a>=0xC2&&a<=0xDF)?1:0;
    if(!n)return '?';
    Uint32 cp=a&((1u<<(6-n))-1u);
    for(int k=0;k<n;k++){
        if(pos>=s.size()||((unsigned char)s[pos]&0xC0)!=0x80)return '?';
        cp=(cp<<6)|((unsigned char)s[pos++]&0x3F);
    }
    return cp;
}
TTF_Font* chooseFace(Uint32 cp,int sz){
    // Standard for latin, Japanese and punctuation; Korean for Hangul.
    int order[5]={0,1,2,3,4};
    if((cp>=0xAC00&&cp<=0xD7AF)||(cp>=0x1100&&cp<=0x11FF)){
        order[0]=3;order[1]=0;order[2]=1;order[3]=2;
    }else if(cp>=0x3400&&cp<=0x9FFF){
        order[0]=1;order[1]=2;order[2]=4;order[3]=0;order[4]=3;
    }
    for(int ix:order){auto* f=scriptFonts[sz][ix];
        if(f&&cp<=65535&&TTF_GlyphIsProvided(f,(Uint16)cp))return f;
    }
    return fonts[sz];
}
std::vector<Run> makeRuns(const std::string& s,int sz){
    std::vector<Run> runs;
    for(size_t i=0;i<s.size();){
        size_t old=i;Uint32 cp=nextUtf8(s,i);
        auto* f=chooseFace(cp,sz);
        bool good=f&&cp<=65535&&TTF_GlyphIsProvided(f,(Uint16)cp);
        std::string frag=good?s.substr(old,i-old):"?";
        if(!runs.empty()&&runs.back().font==f)runs.back().chars+=frag;
        else runs.push_back({frag,f});
    }
    return runs;
}
void text(const std::string& value,int x,int y,int fontSize,Col c,int maxWidth=0){
    if(value.empty()||fontSize<0||fontSize>3||!fonts[fontSize])return;
    std::string cacheKey=std::to_string(fontSize)+":"+std::to_string(c.r)+","+std::to_string(c.g)+","+std::to_string(c.b)+":"+value;
    auto it=textTextures.find(cacheKey);
    if(it==textTextures.end()){
        auto runs=makeRuns(value,fontSize);
        int width=0,height=0;
        for(const auto& run:runs){
            if(!run.font)continue;
            int tw=0,th=0;if(TTF_SizeUTF8(run.font,run.chars.c_str(),&tw,&th)==0){width+=tw;height=std::max(height,th);}
        }
        if(width<=0||height<=0)return;
        // Compose all language faces to one alpha texture: avoids per-frame glyph uploads.
        SDL_Surface* combined=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_RGBA32);
        if(!combined)return;
        SDL_FillRect(combined,nullptr,SDL_MapRGBA(combined->format,0,0,0,0));
        int px=0;
        for(const auto& run:runs){
            if(!run.font)continue;
            SDL_Color fg{c.r,c.g,c.b,c.a};
            SDL_Surface* part=TTF_RenderUTF8_Blended(run.font,run.chars.c_str(),fg);
            if(!part)continue;
            SDL_SetSurfaceBlendMode(part,SDL_BLENDMODE_NONE);
            SDL_Rect dst{px,(height-part->h)/2,part->w,part->h};
            SDL_BlitSurface(part,nullptr,combined,&dst);px+=part->w;
            SDL_FreeSurface(part);
        }
        SDL_Texture* tex=SDL_CreateTextureFromSurface(renderer,combined);SDL_FreeSurface(combined);
        if(!tex)return;
        SDL_SetTextureBlendMode(tex,SDL_BLENDMODE_BLEND);
        CachedText cached{tex,width,height};
        if(textTextures.size()>=300){for(auto& v:textTextures)SDL_DestroyTexture(v.second.tex);textTextures.clear();}
        it=textTextures.emplace(std::move(cacheKey),cached).first;
    }
    SDL_Rect dst{x,y,it->second.w,it->second.h};
    SDL_Rect old{};bool clipped=maxWidth>0;
    bool wasClipped=SDL_RenderIsClipEnabled(renderer);
    if(clipped){SDL_RenderGetClipRect(renderer,&old);SDL_Rect clip{x,y,maxWidth,it->second.h};SDL_RenderSetClipRect(renderer,&clip);}
    SDL_RenderCopy(renderer,it->second.tex,nullptr,&dst);
    if(clipped)SDL_RenderSetClipRect(renderer,wasClipped?&old:nullptr);
}
''' + s[end:]
# Switch shared font and fonts lifecycle.
old='''        PlFontData fd{};
        Result fontRc=plGetSharedFontByType(&fd,PlSharedFontType_ChineseSimplified);
        if(R_FAILED(fontRc))fontRc=plGetSharedFontByType(&fd,PlSharedFontType_Standard);
        if(R_SUCCEEDED(fontRc)){
            constexpr int sizes[]={18,24,32,44};
            for(int i=0;i<4;i++){
                SDL_RWops* memory=SDL_RWFromConstMem(fd.address,fd.size);
                if(memory)fonts[i]=TTF_OpenFontRW(memory,1,sizes[i]);
            }
        }'''
new='''        constexpr int sizes[]={18,24,32,44};
        constexpr PlSharedFontType types[]={PlSharedFontType_Standard,PlSharedFontType_ChineseSimplified,
            PlSharedFontType_ChineseTraditional,PlSharedFontType_KO,PlSharedFontType_ExtChineseSimplified};
        for(int kind=0;kind<5;kind++){
            PlFontData fd{};
            if(R_FAILED(plGetSharedFontByType(&fd,types[kind]))||!fd.address||!fd.size)continue;
            for(int i=0;i<4;i++){
                SDL_RWops* memory=SDL_RWFromConstMem(fd.address,fd.size);
                if(memory)scriptFonts[i][kind]=TTF_OpenFontRW(memory,1,sizes[i]);
            }
        }
        for(int i=0;i<4;i++){
            fonts[i]=scriptFonts[i][0];
            if(!fonts[i])for(int kind=1;kind<5;kind++)if(scriptFonts[i][kind]){fonts[i]=scriptFonts[i][kind];break;}
        }'''
swap(old,new)
swap('for(auto* font:fonts)if(font)TTF_CloseFont(font);','for(auto& row:scriptFonts)for(auto* font:row)if(font)TTF_CloseFont(font);')
# Draw navigation and playback icons geometrically rather than Unicode characters unavailable in Nintendo fonts.
needle='void side(){'
newfunc=r'''void circleOutline(int cx,int cy,int radius,Col c){
    color(c);int px=cx+radius,py=cy;
    for(int angle=1;angle<=64;angle++){
        float a=float(angle)*6.2831853f/64.0f;
        int qx=cx+int(radius*std::cos(a)),qy=cy+int(radius*std::sin(a));
        SDL_RenderDrawLine(renderer,px,py,qx,qy);px=qx;py=qy;
    }
}
void triangleIcon(int cx,int cy,int size,bool right,Col c){
    color(c);
    for(int dx=0;dx<=size;dx++){
        int half=int(float(size-dx)*0.8f);
        int xx=right?cx-size/2+dx:cx+size/2-dx;
        SDL_RenderDrawLine(renderer,xx,cy-half,xx,cy+half);
    }
}
void navIcon(int tabId,int x,int y,Col c){
    if(tabId==0){
        color(c);SDL_RenderDrawLine(renderer,x,y+12,x+15,y);
        SDL_RenderDrawLine(renderer,x+15,y,x+30,y+12);
        SDL_Rect box{x+7,y+12,17,18};SDL_RenderDrawRect(renderer,&box);
    }else if(tabId==1){
        circleOutline(x+12,y+12,10,c);
        color(c);SDL_RenderDrawLine(renderer,x+20,y+20,x+31,y+31);
    }else if(tabId==2){
        circleOutline(x+15,y+14,11,c);text("+",x+9,y+2,1,c);
    }else{
        circleOutline(x+15,y+15,11,c);
        color(c);SDL_Rect inner{x+12,y+12,6,6};SDL_RenderFillRect(renderer,&inner);
    }
}
void controls(int x,int y,bool paused,Col c){
    triangleIcon(x,y,13,false,c);triangleIcon(x-14,y,13,false,c);
    if(paused){triangleIcon(x+82,y,15,true,c);}else{rect(x+76,y-12,7,24,c);rect(x+88,y-12,7,24,c);}
    triangleIcon(x+171,y,13,true,c);triangleIcon(x+185,y,13,true,c);
}
'''
swap(needle,newfunc+needle)
swap('    const char* icons[]={"⌂","⌕","♥","⚙"};\n','')
swap('        text(icons[i],26,y+10,2,tab==i?PINK:GRAY);','        navIcon(i,26,y+13,tab==i?PINK:GRAY);')
swap('round(1176,25,58,49,20,{239,203,216,255});text("♫",1187,29,2,PINK);','round(1176,25,58,49,20,{239,203,216,255});navIcon(2,1191,36,PINK);')
swap('        text("⌕",268,112,1,GRAY);','        navIcon(1,267,113,GRAY);')
swap('        text("♪",x+w/2-19,y+h/2-27,3,WHITE);','        circleOutline(x+w/2,y+h/2,22,WHITE);')
swap('        if(isFav(tracks[j]))text("♥",1113,y+8,1,PINK);\n        text("›",1181,y+3,1,GRAY);','        if(isFav(tracks[j])){circleOutline(1124,y+22,11,PINK);text("+",1117,y+9,0,PINK);}\n        triangleIcon(1189,y+22,7,true,GRAY);')
swap('    text("◀",891,637,1,BLACK);\n    text(playing?"Ⅱ":"▶",978,633,2,PINK);\n    text("▶",1071,637,1,BLACK);\n    text("⋯",1147,628,2,GRAY);','    controls(908,650,playing,BLACK);\n    circleOutline(1167,649,11,GRAY);')
swap('    text("◀",636,477,2,WHITE);text(playing?"Ⅱ":"▶",810,466,3,WHITE);text("▶",986,477,2,WHITE);','    controls(653,491,playing,WHITE);')
# Functional play fallback when sys-tune is absent.
swap('''    if(!connected){setNotice("请先安装并启用 sys-tune 后台服务");return;}
    stopWorker();''','''    if(!connected&&!foregroundReady){setNotice("无法启动音频设备，请检查音频服务");return;}
    stopWorker();''')
swap('''    Result rc=audiusTuneClearQueue();
    if(R_SUCCEEDED(rc))rc=audiusTuneQueueMp3(readyAudio.c_str());
    if(R_SUCCEEDED(rc))rc=audiusTuneSelectFirst();
    if(R_SUCCEEDED(rc))rc=audiusTunePlay();
    if(R_SUCCEEDED(rc)){''','''    Result rc=0;
    bool success=false;
    if(connected){
        rc=audiusTuneClearQueue();
        if(R_SUCCEEDED(rc))rc=audiusTuneQueueMp3(readyAudio.c_str());
        if(R_SUCCEEDED(rc))rc=audiusTuneSelectFirst();
        if(R_SUCCEEDED(rc))rc=audiusTunePlay();
        success=R_SUCCEEDED(rc);
    }else{
        if(localMusic){Mix_HaltMusic();Mix_FreeMusic(localMusic);localMusic=nullptr;}
        localMusic=Mix_LoadMUS(readyAudio.c_str());
        success=localMusic&&Mix_PlayMusic(localMusic,0)==0;
        localPlayback=success;
    }
    if(success){''')
swap('''        notice="播放中（支持 HOME / 游戏内 Tesla 控制）";
    }else{notice="后台服务拒绝播放，错误码: "+std::to_string(rc);}''','''        notice=connected?"播放中 · 可在游戏内用 Tesla 控制":"前台播放中 · 安装 sys-tune 可解锁游戏后台音乐";
    }else{notice=connected?("后台服务播放失败: "+std::to_string(rc)):("MP3 解码失败: "+std::string(Mix_GetError()));}''')
swap('''    for(auto& t:pending){
        if(R_SUCCEEDED(audiusTuneQueueMp3Back(mp3Path(t).c_str()))){''','''    for(auto& t:pending){
        if(!connected)continue;
        if(R_SUCCEEDED(audiusTuneQueueMp3Back(mp3Path(t).c_str()))){''')
swap('''    text(connected?"sys-tune 已连接":"sys-tune 未连接",278,199,1,connected?Col{35,147,97,255}:PINK);
    text("音乐在 sys-tune 中播放；关闭本软件后仍可在 Tesla 侧栏操作",278,232,0,GRAY);''','''    text(connected?"sys-tune 已连接 · 游戏后台播放":"本机前台播放模式",278,199,1,connected?Col{35,147,97,255}:PINK);
    text(connected?"关闭 NRO 后 Tesla 侧栏仍可控制":"可直接试听 MP3；游戏后台播放需安装 sys-tune",278,232,0,GRAY);''')
swap('''    text("需要 Atmosphere / sys-tune / Tesla; 仅在授权内容范围内使用",253,500,0,GRAY);''','''    text("仅在授权内容范围内使用；Tesla / sys-tune 是可选的后台扩展",253,500,0,GRAY);''')
swap('''    if(keys&HidNpadButton_Plus){
        if(connected){if(playing)audiusTunePause();else audiusTunePlay();playing=!playing;}return;
    }
    if(keys&HidNpadButton_ZL){if(connected)audiusTunePrev();return;}
    if(keys&HidNpadButton_ZR){if(connected)audiusTuneNext();return;}''','''    if(keys&HidNpadButton_Plus){
        if(connected){if(playing)audiusTunePause();else audiusTunePlay();playing=!playing;}
        else if(localMusic){if(playing)Mix_PauseMusic();else Mix_ResumeMusic();playing=!playing;}
        return;
    }
    if(keys&HidNpadButton_ZL){if(connected)audiusTunePrev();return;}
    if(keys&HidNpadButton_ZR){
        if(connected)audiusTuneNext();
        else if(!pendingPlaylist.empty()){
            const Track next=pendingPlaylist.front();auto rest=pendingPlaylist;
            rest.erase(rest.begin());playTrack(next,rest);
        }
        return;
    }''')
swap('''    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0)return 1;
    TTF_Init();IMG_Init(IMG_INIT_JPG|IMG_INIT_PNG);''','''    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_AUDIO)!=0)return 1;
    if(!connected)foregroundReady=Mix_OpenAudio(48000,MIX_DEFAULT_FORMAT,2,2048)==0;
    TTF_Init();IMG_Init(IMG_INIT_JPG|IMG_INIT_PNG);''')
swap('''    if(!connected)setNotice("未检测到 sys-tune；可浏览但无法后台播放。详情见设置。");''','''    if(!connected)setNotice(foregroundReady?"前台播放可用 · 按 A 播放，游戏后台播放需 sys-tune":"音频设备初始化失败，请查看设置");''')
swap('''        if(connected&&++tick%90==0){''','''        if(!connected&&localPlayback&&localMusic&&!Mix_PlayingMusic()){
            localPlayback=false;playing=false;
            if(!pendingPlaylist.empty()){
                auto next=pendingPlaylist.front();auto rest=pendingPlaylist;
                rest.erase(rest.begin());playTrack(next,rest);
            }
        }
        if(connected&&++tick%90==0){''')
swap('''    IMG_Quit();TTF_Quit();SDL_Quit();''','''    if(localMusic){Mix_HaltMusic();Mix_FreeMusic(localMusic);localMusic=nullptr;}
    if(foregroundReady)Mix_CloseAudio();
    IMG_Quit();TTF_Quit();SDL_Quit();''')
# Fixed text: don't confuse successful foreground buffering with background availability.
swap('APP_VERSION := 0.1.0-beta2' if False else 'constexpr int W=1280,H=720;', 'constexpr int W=1280,H=720;')
s=s.replace('void cover(const Track& t,','void circleOutline(int cx,int cy,int radius,Col c);\nvoid cover(const Track& t,',1)
p.write_text(s)
mk=root/'Makefile'
s=mk.read_text()
if 'SDL2_mixer' not in s:
    s=s.replace('SDL2_image, SDL2_ttf and curl', 'SDL2_image, SDL2_ttf, SDL2_mixer and curl')
    s=s.replace('APP_VERSION := 0.1.0-beta2','APP_VERSION := 0.1.0-beta3')
    s=s.replace('SDL2_image SDL2_ttf sdl2 libcurl', 'SDL2_image SDL2_ttf SDL2_mixer sdl2 libcurl')
    mk.write_text(s)
print('Patched Beta 3:',p, '(',len(s),' bytes makefile)')