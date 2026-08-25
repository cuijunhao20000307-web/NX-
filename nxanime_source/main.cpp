#include <switch.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
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
    if(!b)return;
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x+w>W)w=W-x; if(y+h>H)h=H-y; if(w<=0||h<=0)return;
    for(int yy=y;yy<y+h;++yy){u32* row=b+yy*stride+x;for(int xx=0;xx<w;++xx)row[xx]=c;}
}
static void border(u32* b,u32 s,int x,int y,int w,int h,int t,u32 c){rect(b,s,x,y,w,t,c);rect(b,s,x,y+h-t,w,t,c);rect(b,s,x,y,t,h,c);rect(b,s,x+w-t,y,t,h,c);}

// -----------------------------------------------------------------------------
// SAFE boot font. This is used before any HOS font/network initialization.
// -----------------------------------------------------------------------------
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
        case '+':G(0,4,4,31,4,4,0);break;case '?':G(14,17,1,2,4,0,4);break;
        case ' ':default:break;
    }
#undef G
}
static void safe_text(u32* b,u32 s,int x,int y,int sc,const char* str,u32 c){
    int ox=x; for(;str&&*str;++str){if(*str=='\n'){y+=9*sc;x=ox;continue;}u8 r[7];glyph(*str,r);for(int gy=0;gy<7;++gy)for(int gx=0;gx<5;++gx)if(r[gy]&(1<<(4-gx)))rect(b,s,x+gx*sc,y+gy*sc,sc,sc,c);x+=6*sc;}
}

// -----------------------------------------------------------------------------
// HOS shared fonts. These are loaded lazily only after the source is connected.
// No font files are bundled with the NRO.
// -----------------------------------------------------------------------------
static bool g_pl_ok=false;
static bool g_font_ok=false;
static FT_Library g_ft=nullptr;
static std::vector<FT_Face> g_faces;
static u32 g_stride_px=0;

static void blend_pixel(u32* fb,int x,int y,u32 fg,u8 alpha){
    if(!fb||x<0||y<0||x>=W||y>=H||alpha==0)return;
    u32* p=fb+y*g_stride_px+x; u32 bg=*p;
    u8 fr=fg&0xff, fg1=(fg>>8)&0xff, fb1=(fg>>16)&0xff;
    u8 br=bg&0xff, bg1=(bg>>8)&0xff, bb=(bg>>16)&0xff;
    u8 r=(u8)((fr*alpha+br*(255-alpha))/255);
    u8 g=(u8)((fg1*alpha+bg1*(255-alpha))/255);
    u8 bl=(u8)((fb1*alpha+bb*(255-alpha))/255);
    *p=rgba(r,g,bl,255);
}

static FT_Face face_for(uint32_t cp){
    for(FT_Face f:g_faces)if(f&&FT_Get_Char_Index(f,cp)!=0)return f;
    return g_faces.empty()?nullptr:g_faces.front();
}

static void set_font_px(int px){for(FT_Face f:g_faces)if(f)FT_Set_Pixel_Sizes(f,0,(FT_UInt)px);}

static int glyph_advance(FT_Face f,uint32_t cp){
    if(!f)return 0; FT_UInt gi=FT_Get_Char_Index(f,cp); if(FT_Load_Glyph(f,gi,FT_LOAD_DEFAULT))return 0;
    int a=(int)(f->glyph->advance.x>>6); return a>0?a:px; // dummy replaced below by fallback
}

static int advance_for(FT_Face f,uint32_t cp,int px){
    if(!f)return px; FT_UInt gi=FT_Get_Char_Index(f,cp); if(FT_Load_Glyph(f,gi,FT_LOAD_DEFAULT))return px;
    int a=(int)(f->glyph->advance.x>>6); return a>0?a:px;
}

static void draw_ft_glyph(u32* fb,FT_Face f,uint32_t cp,int x,int baseline,u32 color){
    if(!f)return; FT_UInt gi=FT_Get_Char_Index(f,cp); if(FT_Load_Glyph(f,gi,FT_LOAD_DEFAULT))return;
    if(FT_Render_Glyph(f->glyph,FT_RENDER_MODE_NORMAL))return;
    FT_GlyphSlot sl=f->glyph; FT_Bitmap* bm=&sl->bitmap; if(bm->pixel_mode!=FT_PIXEL_MODE_GRAY)return;
    int ox=x+sl->bitmap_left, oy=baseline-sl->bitmap_top;
    for(unsigned yy=0;yy<bm->rows;++yy){const unsigned char* row=bm->buffer+yy*bm->pitch;for(unsigned xx=0;xx<bm->width;++xx)blend_pixel(fb,ox+(int)xx,oy+(int)yy,color,row[xx]);}
}

static void hos_text(u32* fb,int x,int baseline,int px,const std::string& str,u32 color,int max_width=0,int max_lines=1){
    if(!g_font_ok){std::string a=str;for(char& c:a)if((unsigned char)c>=128)c='?';safe_text(fb,g_stride_px,x,baseline-px,2,a.c_str(),color);return;}
    set_font_px(px); int start=x, line=1, line_h=px+7;
    for(size_t i=0;i<str.size();){uint32_t cp=0;ssize_t n=decode_utf8(&cp,(const uint8_t*)&str[i]);if(n<=0)break;i+=(size_t)n;
        if(cp=='\r')continue; if(cp=='\n'){if(line>=max_lines)break;++line;x=start;baseline+=line_h;continue;}
        FT_Face f=face_for(cp);int adv=advance_for(f,cp,px);
        if(max_width>0&&x+adv>start+max_width){if(line>=max_lines)break;++line;x=start;baseline+=line_h;}
        draw_ft_glyph(fb,f,cp,x,baseline,color);x+=adv;
    }
}

static bool init_hos_fonts(std::string& err){
    if(g_font_ok)return true;
    log_stage("HOS font init begin");
    Result rc=plInitialize(PlServiceType_User);
    if(R_FAILED(rc)){char b[96];snprintf(b,sizeof(b),"plInitialize failed 0x%08X",rc);err=b;log_stage(b);return false;}
    g_pl_ok=true;
    if(FT_Init_FreeType(&g_ft)){err="FreeType init failed";log_stage(err.c_str());return false;}
    const PlSharedFontType types[]={
        PlSharedFontType_Standard,
        PlSharedFontType_ChineseSimplified,
        PlSharedFontType_ExtChineseSimplified,
        PlSharedFontType_ChineseTraditional
    };
    for(PlSharedFontType t:types){PlFontData d{};if(R_FAILED(plGetSharedFontByType(&d,t)))continue;FT_Face f=nullptr;if(FT_New_Memory_Face(g_ft,(const FT_Byte*)d.address,(FT_Long)d.size,0,&f)==0)g_faces.push_back(f);}
    if(g_faces.empty()){err="No HOS shared font available";log_stage(err.c_str());return false;}
    g_font_ok=true;log_stage("HOS font init ok");return true;
}

static void shutdown_hos_fonts(){
    for(FT_Face f:g_faces)if(f)FT_Done_Face(f);g_faces.clear();
    if(g_ft){FT_Done_FreeType(g_ft);g_ft=nullptr;}g_font_ok=false;
    if(g_pl_ok){plExit();g_pl_ok=false;}
}

enum Screen{SCREEN_SAFE=0,SCREEN_CATALOG,SCREEN_DETAIL,SCREEN_EPISODE};

struct State{
    ProxyConfig proxy{};
    std::vector<AnimeItem> items;
    std::vector<size_t> view;
    AnimeDetail detail;
    Screen screen=SCREEN_SAFE;
    bool socket_ok=false;
    bool provider_ok=false;
    bool connected=false;
    int selected=0;
    int episode_selected=0;
    std::string search;
    std::string status="READY - PRESS A TO CONNECT";
};

static std::string lower_ascii(std::string s){for(char& c:s)if((unsigned char)c<128)c=(char)std::tolower((unsigned char)c);return s;}
static void rebuild_view(State& st){st.view.clear();std::string q=lower_ascii(st.search);for(size_t i=0;i<st.items.size();++i){if(q.empty()||lower_ascii(st.items[i].title).find(q)!=std::string::npos||st.items[i].id.find(st.search)!=std::string::npos)st.view.push_back(i);}if(st.selected>=(int)st.view.size())st.selected=std::max(0,(int)st.view.size()-1);}

static bool keyboard_text(const char* header,const char* guide,const std::string& initial,std::string& out){
    SwkbdConfig k{};if(R_FAILED(swkbdCreate(&k,0)))return false;swkbdConfigMakePresetDefault(&k);swkbdConfigSetHeaderText(&k,header);swkbdConfigSetGuideText(&k,guide);swkbdConfigSetInitialText(&k,initial.c_str());char b[768]={0};Result rc=swkbdShow(&k,b,sizeof(b));swkbdClose(&k);if(R_FAILED(rc))return false;out=b;return true;
}

static void draw_safe(u32* b,u32 s,const State& st){
    u32 bg=rgba(9,10,14),panel=rgba(20,22,30),white=rgba(244,245,248),muted=rgba(150,156,170),pink=rgba(255,62,126),green=rgba(55,205,120);
    rect(b,s,0,0,W,H,bg);rect(b,s,0,0,W,92,panel);rect(b,s,0,90,W,2,pink);
    safe_text(b,s,42,28,6,"NXANIME",white);safe_text(b,s,330,34,3,"HOS FONT BUILD 0.2",pink);
    safe_text(b,s,42,128,4,"GIRIGIRI PROVIDER",white);safe_text(b,s,42,174,2,"SAFE BOOT - HOS FONT LOADS AFTER CONNECT",muted);
    rect(b,s,42,220,1196,90,panel);border(b,s,42,220,1196,90,2,st.connected?green:rgba(65,70,82));
    safe_text(b,s,68,245,3,st.connected?"SOURCE CONNECTED":"SOURCE NOT CONNECTED",st.connected?green:muted);
    char count[96];snprintf(count,sizeof(count),"CATALOG ITEMS: %zu",st.items.size());safe_text(b,s,68,280,2,count,white);
    rect(b,s,42,335,1196,180,panel);safe_text(b,s,68,362,3,"STATUS",pink);
    std::string ascii=st.status;for(char& c:ascii)if((unsigned char)c>=128)c='?';safe_text(b,s,68,405,2,ascii.c_str(),white);
    char mode[160];snprintf(mode,sizeof(mode),"PROXY MODE: %s",proxy_mode_name(st.proxy.mode));safe_text(b,s,68,455,2,mode,muted);
    rect(b,s,42,540,1196,62,panel);safe_text(b,s,68,558,2,"A CONNECT",white);safe_text(b,s,330,558,2,"ZR PROXY MODE",muted);safe_text(b,s,650,558,2,"Y TEST SOURCE",muted);safe_text(b,s,1080,558,2,"+ EXIT",white);
    rect(b,s,0,635,W,85,panel);safe_text(b,s,42,660,2,"LOG: SDMC:/SWITCH/NXANIME/STARTUP.LOG",muted);
}

static void header(u32* b,const char* page){
    u32 bg=rgba(9,10,14),panel=rgba(19,21,29),white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126);
    rect(b,g_stride_px,0,0,W,H,bg);rect(b,g_stride_px,0,0,W,88,panel);rect(b,g_stride_px,0,86,W,2,pink);
    hos_text(b,40,57,36,"NXAnime",white,260,1);hos_text(b,220,55,18,"GiriGiri",pink,160,1);hos_text(b,1040,54,17,page?page:"",muted,200,1);
}

static void footer(u32* b,const State& st,const std::string& hints){u32 panel=rgba(19,21,29),white=rgba(238,239,244),muted=rgba(145,150,164);rect(b,g_stride_px,0,640,W,80,panel);hos_text(b,38,674,17,st.status,muted,760,1);hos_text(b,820,674,17,hints,white,420,1);}

static void draw_catalog(u32* b,const State& st){
    header(b,"首页");u32 white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126),card=rgba(24,27,37),sel=rgba(37,40,53);
    hos_text(b,40,132,25,"动漫目录",white,300,1);hos_text(b,40,165,17,st.search.empty()?"GiriGiri 公共目录 · HOS 系统字体":("搜索："+st.search),muted,760,1);
    if(st.view.empty()){rect(b,g_stride_px,40,205,1200,360,card);hos_text(b,75,285,28,"没有可显示的番剧",white,700,1);hos_text(b,75,330,19,"按 Y 刷新，X 搜索，ZR 切换代理模式。",muted,900,2);footer(b,st,"Y 刷新   + 退出");return;}
    int rows=7;int start=st.selected>=rows?st.selected-rows+1:0;
    for(int r=0;r<rows;++r){int vi=start+r;if(vi>=(int)st.view.size())break;const AnimeItem& it=st.items[st.view[vi]];int y=190+r*62;bool on=vi==st.selected;rect(b,g_stride_px,40,y,1200,54,on?sel:card);if(on)rect(b,g_stride_px,40,y,6,54,pink);hos_text(b,66,y+36,21,it.title,white,900,1);hos_text(b,1040,y+34,15,it.id,on?pink:muted,170,1);}
    char c[64];snprintf(c,sizeof(c),"%d / %zu",st.selected+1,st.view.size());hos_text(b,1080,615,15,c,muted,150,1);footer(b,st,"A 详情  X 搜索  Y 刷新");
}

static void draw_detail(u32* b,const State& st){
    header(b,"详情");u32 white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126),card=rgba(24,27,37),sel=rgba(37,40,53);
    hos_text(b,40,135,29,st.detail.title,white,1170,1);hos_text(b,40,170,17,st.detail.status.empty()?st.detail.id:(st.detail.status+"  ·  "+st.detail.id),pink,1000,1);
    rect(b,g_stride_px,40,195,1200,125,card);hos_text(b,62,228,17,st.detail.description.empty()?"暂无简介":st.detail.description,muted,1140,4);
    hos_text(b,40,360,21,"选集",white,200,1);
    if(st.detail.episodes.empty())hos_text(b,40,410,19,"没有解析到公开集数入口。",muted,760,1);
    else{const int cols=8;int start=(st.episode_selected/24)*24;for(int i=0;i<24;++i){int ei=start+i;if(ei>=(int)st.detail.episodes.size())break;int x=40+(i%cols)*149,y=392+(i/cols)*70;bool on=ei==st.episode_selected;rect(b,g_stride_px,x,y,136,58,on?sel:card);border(b,g_stride_px,x,y,136,58,on?3:1,on?pink:rgba(62,66,82));hos_text(b,x+14,y+37,17,st.detail.episodes[ei].label,on?white:muted,108,1);}}
    footer(b,st,"A 选择   B 返回");
}

static void draw_episode(u32* b,const State& st){
    header(b,"播放");u32 white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126),card=rgba(24,27,37);
    std::string label="集数";std::string url;if(!st.detail.episodes.empty()&&st.episode_selected<(int)st.detail.episodes.size()){label=st.detail.episodes[st.episode_selected].label;url=st.detail.episodes[st.episode_selected].url;}
    hos_text(b,40,145,29,st.detail.title+" · "+label,white,1160,1);rect(b,g_stride_px,40,190,1200,330,card);hos_text(b,70,245,23,"播放器接口",pink,500,1);hos_text(b,70,295,18,"当前版本已经完成目录、详情和选集浏览。播放器核心将在下一阶段接入公开、非 DRM 或本地视频源。",muted,1100,3);hos_text(b,70,405,16,url,muted,1100,2);footer(b,st,"B 返回");
}

static void draw_frame(Framebuffer& fb,const State& st){u32 stride=0;u32* b=(u32*)framebufferBegin(&fb,&stride);g_stride_px=stride/sizeof(u32);if(st.screen==SCREEN_SAFE)draw_safe(b,g_stride_px,st);else if(st.screen==SCREEN_CATALOG)draw_catalog(b,st);else if(st.screen==SCREEN_DETAIL)draw_detail(b,st);else draw_episode(b,st);framebufferEnd(&fb);}

static void ensure_network(State& st){
    if(!st.socket_ok){log_stage("socketInitializeDefault begin");Result rc=socketInitializeDefault();if(R_FAILED(rc)){char t[96];snprintf(t,sizeof(t),"SOCKET INIT FAILED 0X%08X",rc);st.status=t;log_stage(t);return;}st.socket_ok=true;log_stage("socketInitializeDefault ok");}
    if(!st.provider_ok){log_stage("provider_init begin");std::string msg;if(!provider_init(msg)){st.status="PROVIDER INIT FAILED: "+msg;log_stage(st.status.c_str());return;}st.provider_ok=true;log_stage("provider_init ok");}
}

static void connect_source(State& st){
    ensure_network(st);if(!st.socket_ok||!st.provider_ok)return;log_stage("provider_fetch_home begin");std::vector<AnimeItem> out;std::string msg;
    if(provider_fetch_home(st.proxy,out,msg)){st.items.swap(out);st.connected=true;st.status=msg;rebuild_view(st);log_stage("provider_fetch_home ok");std::string ferr;if(init_hos_fonts(ferr)){st.screen=SCREEN_CATALOG;st.status=msg+" · HOS 字体已加载";}else{st.status=msg+" · HOS FONT FALLBACK: "+ferr;log_stage("HOS font fallback active");}}
    else{st.connected=false;st.status=msg;log_stage(("provider_fetch_home failed: "+msg).c_str());}
}

static void refresh_catalog(State& st){if(!st.connected){connect_source(st);return;}std::vector<AnimeItem> out;std::string msg;if(provider_fetch_home(st.proxy,out,msg)){st.items.swap(out);rebuild_view(st);st.status=msg;}else st.status=msg;}

static void open_detail(State& st){if(st.view.empty())return;const AnimeItem item=st.items[st.view[st.selected]];std::string msg;AnimeDetail d;if(provider_fetch_detail(st.proxy,item,d,msg)){st.detail=std::move(d);st.episode_selected=0;st.screen=SCREEN_DETAIL;}st.status=msg;}

static void do_search(State& st){std::string q=st.search;if(!keyboard_text("搜索番剧","搜索当前已加载目录；留空显示全部",q,q))return;st.search=q;st.selected=0;rebuild_view(st);st.status=q.empty()?"已清除搜索":("搜索："+q);}

int main(int argc,char** argv){
    (void)argc;(void)argv;mkdir(LOG_DIR,0777);FILE* lf=fopen(LOG_PATH,"w");if(lf){fprintf(lf,"NXAnime 0.2 startup\n");fclose(lf);}log_stage("main entered");
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);log_stage("pad ok");
    Framebuffer fb;log_stage("framebufferCreate begin");Result frc=framebufferCreate(&fb,nwindowGetDefault(),W,H,PIXEL_FORMAT_RGBA_8888,2);if(R_FAILED(frc)){log_stage("framebufferCreate failed");return (int)frc;}framebufferMakeLinear(&fb);log_stage("framebuffer ok");
    State st;proxy_config_load(&st.proxy);log_stage("proxy config loaded");draw_frame(fb,st);log_stage("first frame rendered");
    while(appletMainLoop()){
        padUpdate(&pad);u64 d=padGetButtonsDown(&pad);if(d&HidNpadButton_Plus)break;
        if(st.screen==SCREEN_SAFE){
            if(d&HidNpadButton_A){st.status="CONNECTING GIRIGIRI...";draw_frame(fb,st);connect_source(st);}
            if(d&HidNpadButton_Y){ensure_network(st);if(st.socket_ok&&st.provider_ok){std::string m;bool ok=provider_test_source(st.proxy,m);st.status=(ok?"SOURCE TEST OK: ":"SOURCE TEST FAILED: ")+m;}}
            if(d&HidNpadButton_ZR){st.proxy.mode=(ProxyMode)(((int)st.proxy.mode+1)%3);proxy_config_save(&st.proxy);st.status=std::string("PROXY MODE: ")+proxy_mode_name(st.proxy.mode);}
        }else if(st.screen==SCREEN_CATALOG){
            if(!st.view.empty()){if(d&HidNpadButton_Up)st.selected=(st.selected-1+(int)st.view.size())%(int)st.view.size();if(d&HidNpadButton_Down)st.selected=(st.selected+1)%(int)st.view.size();if(d&HidNpadButton_A)open_detail(st);}
            if(d&HidNpadButton_X)do_search(st);if(d&HidNpadButton_Y)refresh_catalog(st);if(d&HidNpadButton_ZR){st.proxy.mode=(ProxyMode)(((int)st.proxy.mode+1)%3);proxy_config_save(&st.proxy);st.status=std::string("代理模式：")+proxy_mode_name(st.proxy.mode);}
        }else if(st.screen==SCREEN_DETAIL){int n=(int)st.detail.episodes.size();if(n>0){if(d&HidNpadButton_Left)st.episode_selected=(st.episode_selected-1+n)%n;if(d&HidNpadButton_Right)st.episode_selected=(st.episode_selected+1)%n;if(d&HidNpadButton_Up)st.episode_selected=std::max(0,st.episode_selected-8);if(d&HidNpadButton_Down)st.episode_selected=std::min(n-1,st.episode_selected+8);if(d&HidNpadButton_A)st.screen=SCREEN_EPISODE;}if(d&HidNpadButton_B)st.screen=SCREEN_CATALOG;
        }else if(st.screen==SCREEN_EPISODE){if(d&HidNpadButton_B)st.screen=SCREEN_DETAIL;}
        draw_frame(fb,st);svcSleepThread(12'000'000);
    }
    log_stage("shutdown begin");if(st.provider_ok)provider_exit();if(st.socket_ok)socketExit();shutdown_hos_fonts();framebufferClose(&fb);log_stage("shutdown complete");return 0;
}
