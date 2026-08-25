#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "provider.hpp"
#include "proxy.h"

#define W 1280
#define H 720
#define LOG_DIR  "sdmc:/switch/NXAnime"
#define LOG_PATH "sdmc:/switch/NXAnime/startup.log"

static inline u32 rgba(u8 r,u8 g,u8 b,u8 a=255){return (u32)r|((u32)g<<8)|((u32)b<<16)|((u32)a<<24);}

static void log_stage(const char* s){
    mkdir(LOG_DIR,0777);
    FILE* f=fopen(LOG_PATH,"a");
    if(f){fprintf(f,"%s\n",s?s:"");fclose(f);}
}

static void rect(u32* b,u32 stride,int x,int y,int w,int h,u32 c){
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x+w>W)w=W-x; if(y+h>H)h=H-y; if(w<=0||h<=0)return;
    for(int yy=y;yy<y+h;++yy){u32* row=b+yy*stride+x;for(int xx=0;xx<w;++xx)row[xx]=c;}
}
static void border(u32* b,u32 s,int x,int y,int w,int h,int t,u32 c){rect(b,s,x,y,w,t,c);rect(b,s,x,y+h-t,w,t,c);rect(b,s,x,y,t,h,c);rect(b,s,x+w-t,y,t,h,c);}

static void glyph(char ch,u8 out[7]){
    memset(out,0,7); if(ch>='a'&&ch<='z')ch-=32;
#define G(a,b,c,d,e,f,g) do{out[0]=a;out[1]=b;out[2]=c;out[3]=d;out[4]=e;out[5]=f;out[6]=g;}while(0)
    switch(ch){
        case 'A':G(14,17,17,31,17,17,17);break;case 'B':G(30,17,17,30,17,17,30);break;
        case 'C':G(14,17,16,16,16,17,14);break;case 'D':G(30,17,17,17,17,17,30);break;
        case 'E':G(31,16,16,30,16,16,31);break;case 'F':G(31,16,16,30,16,16,16);break;
        case 'G':G(14,17,16,23,17,17,15);break;case 'H':G(17,17,17,31,17,17,17);break;
        case 'I':G(31,4,4,4,4,4,31);break;case 'J':G(7,2,2,2,18,18,12);break;
        case 'K':G(17,18,20,24,20,18,17);break;case 'L':G(16,16,16,16,16,16,31);break;
        case 'M':G(17,27,21,21,17,17,17);break;case 'N':G(17,25,21,19,17,17,17);break;
        case 'O':G(14,17,17,17,17,17,14);break;case 'P':G(30,17,17,30,16,16,16);break;
        case 'Q':G(14,17,17,17,21,18,13);break;case 'R':G(30,17,17,30,20,18,17);break;
        case 'S':G(15,16,16,14,1,1,30);break;case 'T':G(31,4,4,4,4,4,4);break;
        case 'U':G(17,17,17,17,17,17,14);break;case 'V':G(17,17,17,17,17,10,4);break;
        case 'W':G(17,17,17,21,21,21,10);break;case 'X':G(17,17,10,4,10,17,17);break;
        case 'Y':G(17,17,10,4,4,4,4);break;case 'Z':G(31,1,2,4,8,16,31);break;
        case '0':G(14,17,19,21,25,17,14);break;case '1':G(4,12,4,4,4,4,14);break;
        case '2':G(14,17,1,2,4,8,31);break;case '3':G(30,1,1,14,1,1,30);break;
        case '4':G(2,6,10,18,31,2,2);break;case '5':G(31,16,16,30,1,1,30);break;
        case '6':G(14,16,16,30,17,17,14);break;case '7':G(31,1,2,4,8,8,8);break;
        case '8':G(14,17,17,14,17,17,14);break;case '9':G(14,17,17,15,1,1,14);break;
        case ':':G(0,4,4,0,4,4,0);break;case '.':G(0,0,0,0,0,6,6);break;
        case '/':G(1,2,2,4,8,8,16);break;case '-':G(0,0,0,31,0,0,0);break;
        case '+':G(0,4,4,31,4,4,0);break;case '?':G(14,17,1,2,4,0,4);break;default:break;
    }
#undef G
}
static void text(u32* b,u32 s,int x,int y,int sc,const char* str,u32 c){
    int ox=x; for(;str&&*str;++str){if(*str=='\n'){y+=9*sc;x=ox;continue;}u8 r[7];glyph(*str,r);for(int gy=0;gy<7;++gy)for(int gx=0;gx<5;++gx)if(r[gy]&(1<<(4-gx)))rect(b,s,x+gx*sc,y+gy*sc,sc,sc,c);x+=6*sc;}
}

struct State{
    ProxyConfig proxy{};
    std::vector<AnimeItem> items;
    bool socket_ok=false;
    bool provider_ok=false;
    bool connected=false;
    std::string status="READY - PRESS A TO CONNECT";
};

static void draw(u32* b,u32 s,const State& st){
    u32 bg=rgba(9,10,14),panel=rgba(20,22,30),white=rgba(244,245,248),muted=rgba(150,156,170),pink=rgba(255,62,126),green=rgba(55,205,120),red=rgba(235,75,75);
    rect(b,s,0,0,W,H,bg);rect(b,s,0,0,W,92,panel);rect(b,s,0,90,W,2,pink);
    text(b,s,42,28,6,"NXANIME",white);text(b,s,330,34,3,"SAFE BUILD 0.1.1",pink);
    text(b,s,42,128,4,"GIRIGIRI PROVIDER",white);
    text(b,s,42,174,2,"LAZY NETWORK INIT - NO AUTO FETCH",muted);

    rect(b,s,42,220,1196,90,panel);border(b,s,42,220,1196,90,2,st.connected?green:rgba(65,70,82));
    text(b,s,68,245,3,st.connected?"SOURCE CONNECTED":"SOURCE NOT CONNECTED",st.connected?green:muted);
    char count[96];snprintf(count,sizeof(count),"CATALOG ITEMS: %zu",st.items.size());text(b,s,68,280,2,count,white);

    rect(b,s,42,335,1196,180,panel);
    text(b,s,68,362,3,"STATUS",pink);
    std::string ascii=st.status;for(char& c:ascii)if((unsigned char)c>=128)c='?';
    text(b,s,68,405,2,ascii.c_str(),white);
    char mode[160];snprintf(mode,sizeof(mode),"PROXY MODE: %s",proxy_mode_name(st.proxy.mode));text(b,s,68,455,2,mode,muted);

    rect(b,s,42,540,1196,62,panel);
    text(b,s,68,558,2,"A CONNECT / REFRESH",white);
    text(b,s,390,558,2,"ZR CYCLE PROXY MODE",muted);
    text(b,s,790,558,2,"Y TEST SOURCE",muted);
    text(b,s,1080,558,2,"+ EXIT",white);

    rect(b,s,0,635,W,85,panel);
    text(b,s,42,660,2,"LOG: SDMC:/SWITCH/NXANIME/STARTUP.LOG",muted);
}

static void draw_frame(Framebuffer& fb,const State& st){u32 stride=0;u32* b=(u32*)framebufferBegin(&fb,&stride);draw(b,stride/4,st);framebufferEnd(&fb);}

static void ensure_network(State& st){
    if(!st.socket_ok){
        log_stage("socketInitializeDefault begin");
        Result rc=socketInitializeDefault();
        if(R_FAILED(rc)){char t[96];snprintf(t,sizeof(t),"SOCKET INIT FAILED 0X%08X",rc);st.status=t;log_stage(t);return;}
        st.socket_ok=true;log_stage("socketInitializeDefault ok");
    }
    if(!st.provider_ok){
        log_stage("provider_init begin");std::string msg;
        if(!provider_init(msg)){st.status="PROVIDER INIT FAILED: "+msg;log_stage(st.status.c_str());return;}
        st.provider_ok=true;log_stage("provider_init ok");
    }
}

static void connect_source(State& st){
    ensure_network(st);if(!st.socket_ok||!st.provider_ok)return;
    log_stage("provider_fetch_home begin");std::vector<AnimeItem> out;std::string msg;
    if(provider_fetch_home(st.proxy,out,msg)){st.items.swap(out);st.connected=true;st.status=msg;log_stage("provider_fetch_home ok");}
    else{st.connected=false;st.status=msg;log_stage(("provider_fetch_home failed: "+msg).c_str());}
}

int main(int argc,char** argv){
    (void)argc;(void)argv;
    mkdir(LOG_DIR,0777);FILE* lf=fopen(LOG_PATH,"w");if(lf){fprintf(lf,"NXAnime 0.1.1 startup\n");fclose(lf);}log_stage("main entered");
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);log_stage("pad ok");

    Framebuffer fb;log_stage("framebufferCreate begin");Result frc=framebufferCreate(&fb,nwindowGetDefault(),W,H,PIXEL_FORMAT_RGBA_8888,2);
    if(R_FAILED(frc)){log_stage("framebufferCreate failed");return (int)frc;}framebufferMakeLinear(&fb);log_stage("framebuffer ok");

    State st;proxy_config_load(&st.proxy);log_stage("proxy config loaded");draw_frame(fb,st);log_stage("first frame rendered");

    while(appletMainLoop()){
        padUpdate(&pad);u64 down=padGetButtonsDown(&pad);if(down&HidNpadButton_Plus)break;
        if(down&HidNpadButton_A){st.status="CONNECTING GIRIGIRI...";draw_frame(fb,st);connect_source(st);}
        if(down&HidNpadButton_Y){ensure_network(st);if(st.socket_ok&&st.provider_ok){std::string m;bool ok=provider_test_source(st.proxy,m);st.status=(ok?"SOURCE TEST OK: ":"SOURCE TEST FAILED: ")+m;}}
        if(down&HidNpadButton_ZR){st.proxy.mode=(ProxyMode)(((int)st.proxy.mode+1)%3);proxy_config_save(&st.proxy);st.status=std::string("PROXY MODE: ")+proxy_mode_name(st.proxy.mode);}
        draw_frame(fb,st);svcSleepThread(12'000'000);
    }

    log_stage("shutdown begin");if(st.provider_ok)provider_exit();if(st.socket_ok)socketExit();framebufferClose(&fb);log_stage("shutdown complete");return 0;
}
