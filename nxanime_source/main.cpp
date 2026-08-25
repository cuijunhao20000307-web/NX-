#include <switch.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>

#include "cover.hpp"
#include "provider.hpp"
#include "proxy.h"

#define W 1280
#define H 720
#define LOG_DIR  "sdmc:/switch/NXAnime"
#define LOG_PATH "sdmc:/switch/NXAnime/startup.log"

static inline u32 rgba(u8 r,u8 g,u8 b,u8 a=255){return (u32)r|((u32)g<<8)|((u32)b<<16)|((u32)a<<24);}
static void log_stage(const char* s){mkdir(LOG_DIR,0777);FILE* f=fopen(LOG_PATH,"a");if(f){fprintf(f,"%s\n",s?s:"");fclose(f);}}

static void rect(u32* b,u32 stride,int x,int y,int w,int h,u32 c){
    if(!b)return;if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}if(x+w>W)w=W-x;if(y+h>H)h=H-y;if(w<=0||h<=0)return;
    for(int yy=y;yy<y+h;++yy){u32* row=b+yy*stride+x;for(int xx=0;xx<w;++xx)row[xx]=c;}
}
static void border(u32* b,u32 s,int x,int y,int w,int h,int t,u32 c){rect(b,s,x,y,w,t,c);rect(b,s,x,y+h-t,w,t,c);rect(b,s,x,y,t,h,c);rect(b,s,x+w-t,y,t,h,c);}

// SAFE boot font (ASCII only) so startup never depends on HOS font services.
static void glyph(char ch,u8 out[7]){
    memset(out,0,7);if(ch>='a'&&ch<='z')ch-=32;
#define G(a,b,c,d,e,f,g) do{out[0]=a;out[1]=b;out[2]=c;out[3]=d;out[4]=e;out[5]=f;out[6]=g;}while(0)
    switch(ch){
        case 'A':G(14,17,17,31,17,17,17);break;case 'B':G(30,17,17,30,17,17,30);break;case 'C':G(14,17,16,16,16,17,14);break;
        case 'D':G(30,17,17,17,17,17,30);break;case 'E':G(31,16,16,30,16,16,31);break;case 'F':G(31,16,16,30,16,16,16);break;
        case 'G':G(14,17,16,23,17,17,15);break;case 'H':G(17,17,17,31,17,17,17);break;case 'I':G(31,4,4,4,4,4,31);break;
        case 'J':G(7,2,2,2,18,18,12);break;case 'K':G(17,18,20,24,20,18,17);break;case 'L':G(16,16,16,16,16,16,31);break;
        case 'M':G(17,27,21,21,17,17,17);break;case 'N':G(17,25,21,19,17,17,17);break;case 'O':G(14,17,17,17,17,17,14);break;
        case 'P':G(30,17,17,30,16,16,16);break;case 'Q':G(14,17,17,17,21,18,13);break;case 'R':G(30,17,17,30,20,18,17);break;
        case 'S':G(15,16,16,14,1,1,30);break;case 'T':G(31,4,4,4,4,4,4);break;case 'U':G(17,17,17,17,17,17,14);break;
        case 'V':G(17,17,17,17,17,10,4);break;case 'W':G(17,17,17,21,21,21,10);break;case 'X':G(17,17,10,4,10,17,17);break;
        case 'Y':G(17,17,10,4,4,4,4);break;case 'Z':G(31,1,2,4,8,16,31);break;
        case '0':G(14,17,19,21,25,17,14);break;case '1':G(4,12,4,4,4,4,14);break;case '2':G(14,17,1,2,4,8,31);break;
        case '3':G(30,1,1,14,1,1,30);break;case '4':G(2,6,10,18,31,2,2);break;case '5':G(31,16,16,30,1,1,30);break;
        case '6':G(14,16,16,30,17,17,14);break;case '7':G(31,1,2,4,8,8,8);break;case '8':G(14,17,17,14,17,17,14);break;
        case '9':G(14,17,17,15,1,1,14);break;case ':':G(0,4,4,0,4,4,0);break;case '.':G(0,0,0,0,0,6,6);break;
        case '/':G(1,2,2,4,8,8,16);break;case '-':G(0,0,0,31,0,0,0);break;case '+':G(0,4,4,31,4,4,0);break;
        case '?':G(14,17,1,2,4,0,4);break;default:break;
    }
#undef G
}
static void safe_text(u32* b,u32 s,int x,int y,int sc,const char* str,u32 c){int ox=x;for(;str&&*str;++str){if(*str=='\n'){y+=9*sc;x=ox;continue;}u8 r[7];glyph(*str,r);for(int gy=0;gy<7;++gy)for(int gx=0;gx<5;++gx)if(r[gy]&(1<<(4-gx)))rect(b,s,x+gx*sc,y+gy*sc,sc,sc,c);x+=6*sc;}}

static bool g_pl_ok=false,g_font_ok=false;static FT_Library g_ft=nullptr;static std::vector<FT_Face> g_faces;static u32 g_stride=0;
static void blend(u32* fb,int x,int y,u32 fg,u8 a){if(!fb||x<0||y<0||x>=W||y>=H||!a)return;u32* p=fb+y*g_stride+x;u32 bg=*p;u8 fr=fg&255,fg1=(fg>>8)&255,fb1=(fg>>16)&255,br=bg&255,bg1=(bg>>8)&255,bb=(bg>>16)&255;*p=rgba((fr*a+br*(255-a))/255,(fg1*a+bg1*(255-a))/255,(fb1*a+bb*(255-a))/255);}
static FT_Face face_for(uint32_t cp){for(auto f:g_faces)if(f&&FT_Get_Char_Index(f,cp))return f;return g_faces.empty()?nullptr:g_faces.front();}
static void set_px(int px){for(auto f:g_faces)if(f)FT_Set_Pixel_Sizes(f,0,px);}
static int advance_for(FT_Face f,uint32_t cp,int px){if(!f)return px;FT_UInt gi=FT_Get_Char_Index(f,cp);if(FT_Load_Glyph(f,gi,FT_LOAD_DEFAULT))return px;int a=f->glyph->advance.x>>6;return a>0?a:px;}
static void ft_glyph(u32* fb,FT_Face f,uint32_t cp,int x,int base,u32 color){if(!f)return;FT_UInt gi=FT_Get_Char_Index(f,cp);if(FT_Load_Glyph(f,gi,FT_LOAD_DEFAULT)||FT_Render_Glyph(f->glyph,FT_RENDER_MODE_NORMAL))return;auto sl=f->glyph;auto* bm=&sl->bitmap;if(bm->pixel_mode!=FT_PIXEL_MODE_GRAY)return;int ox=x+sl->bitmap_left,oy=base-sl->bitmap_top;for(unsigned yy=0;yy<bm->rows;++yy){const u8* row=bm->buffer+yy*bm->pitch;for(unsigned xx=0;xx<bm->width;++xx)blend(fb,ox+xx,oy+yy,color,row[xx]);}}
static void text(u32* fb,int x,int base,int px,const std::string& str,u32 color,int maxw=0,int maxlines=1){
    if(!g_font_ok){std::string a=str;for(char& c:a)if((u8)c>=128)c='?';safe_text(fb,g_stride,x,base-px,2,a.c_str(),color);return;}
    set_px(px);int sx=x,line=1,lh=px+7;for(size_t i=0;i<str.size();){uint32_t cp=0;ssize_t n=decode_utf8(&cp,(const u8*)&str[i]);if(n<=0)break;i+=n;if(cp=='\r')continue;if(cp=='\n'){if(line>=maxlines)break;++line;x=sx;base+=lh;continue;}FT_Face f=face_for(cp);int a=advance_for(f,cp,px);if(maxw>0&&x+a>sx+maxw){if(line>=maxlines)break;++line;x=sx;base+=lh;}ft_glyph(fb,f,cp,x,base,color);x+=a;}
}
static bool init_hos_fonts(std::string& err){
    if(g_font_ok)return true;log_stage("HOS font init begin");Result rc=plInitialize(PlServiceType_User);if(R_FAILED(rc)){char b[96];snprintf(b,sizeof(b),"plInitialize 0x%08X",rc);err=b;log_stage(b);return false;}g_pl_ok=true;
    if(FT_Init_FreeType(&g_ft)){err="FreeType init failed";return false;}
    const PlSharedFontType ts[]={PlSharedFontType_Standard,PlSharedFontType_ChineseSimplified,PlSharedFontType_ExtChineseSimplified,PlSharedFontType_ChineseTraditional};
    for(auto t:ts){PlFontData d{};if(R_FAILED(plGetSharedFontByType(&d,t)))continue;FT_Face f=nullptr;if(!FT_New_Memory_Face(g_ft,(const FT_Byte*)d.address,d.size,0,&f))g_faces.push_back(f);}
    if(g_faces.empty()){err="No HOS font";return false;}g_font_ok=true;log_stage("HOS font init ok");return true;
}
static void shutdown_fonts(){for(auto f:g_faces)if(f)FT_Done_Face(f);g_faces.clear();if(g_ft){FT_Done_FreeType(g_ft);g_ft=nullptr;}if(g_pl_ok){plExit();g_pl_ok=false;}g_font_ok=false;}

enum Screen{SAFE=0,CATALOG,DETAIL,EPISODE};
struct State{
    ProxyConfig proxy{};
    std::vector<AnimeItem> items;
    std::vector<size_t> view;
    AnimeDetail detail;
    CoverImage cover;
    std::string cover_id;
    Screen screen=SAFE;
    bool socket_ok=false,provider_ok=false,connected=false;
    int selected=0,ep=0;
    std::string search,status="READY - PRESS A TO CONNECT";
};

static std::string lower_ascii(std::string s){for(char& c:s)if((u8)c<128)c=std::tolower((u8)c);return s;}
static void rebuild(State& st){st.view.clear();std::string q=lower_ascii(st.search);for(size_t i=0;i<st.items.size();++i){if(q.empty()||lower_ascii(st.items[i].title).find(q)!=std::string::npos||st.items[i].id.find(st.search)!=std::string::npos)st.view.push_back(i);}if(st.selected>=(int)st.view.size())st.selected=std::max(0,(int)st.view.size()-1);}
static bool keyboard(const char* h,const char* g,const std::string& initial,std::string& out){SwkbdConfig k{};if(R_FAILED(swkbdCreate(&k,0)))return false;swkbdConfigMakePresetDefault(&k);swkbdConfigSetHeaderText(&k,h);swkbdConfigSetGuideText(&k,g);swkbdConfigSetInitialText(&k,initial.c_str());char b[768]={0};Result rc=swkbdShow(&k,b,sizeof(b));swkbdClose(&k);if(R_FAILED(rc))return false;out=b;return true;}

static void header(u32* b,const std::string& page){u32 bg=rgba(9,10,14),panel=rgba(19,21,29),white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126);rect(b,g_stride,0,0,W,H,bg);rect(b,g_stride,0,0,W,88,panel);rect(b,g_stride,0,86,W,2,pink);text(b,40,57,36,"NXAnime",white,260);text(b,220,55,18,"GiriGiri",pink,180);text(b,1030,54,17,page+" · v0.3",muted,220);}
static void footer(u32* b,const State& st,const std::string& hint){u32 panel=rgba(19,21,29),white=rgba(238,239,244),muted=rgba(145,150,164);rect(b,g_stride,0,640,W,80,panel);text(b,38,674,17,st.status,muted,760);text(b,820,674,17,hint,white,420);}

static void draw_safe(u32* b,u32 s,const State& st){u32 bg=rgba(9,10,14),panel=rgba(20,22,30),white=rgba(244,245,248),muted=rgba(150,156,170),pink=rgba(255,62,126),green=rgba(55,205,120);rect(b,s,0,0,W,H,bg);rect(b,s,0,0,W,92,panel);rect(b,s,0,90,W,2,pink);safe_text(b,s,42,28,6,"NXANIME",white);safe_text(b,s,330,34,3,"COVER BUILD 0.3",pink);safe_text(b,s,42,128,4,"GIRIGIRI PROVIDER",white);safe_text(b,s,42,174,2,"SAFE BOOT - HOS FONT AND COVER CACHE AFTER CONNECT",muted);rect(b,s,42,220,1196,90,panel);border(b,s,42,220,1196,90,2,st.connected?green:rgba(65,70,82));safe_text(b,s,68,245,3,st.connected?"SOURCE CONNECTED":"SOURCE NOT CONNECTED",st.connected?green:muted);char c[80];snprintf(c,sizeof(c),"CATALOG ITEMS: %zu",st.items.size());safe_text(b,s,68,280,2,c,white);rect(b,s,42,335,1196,180,panel);safe_text(b,s,68,362,3,"STATUS",pink);std::string a=st.status;for(char& x:a)if((u8)x>=128)x='?';safe_text(b,s,68,405,2,a.c_str(),white);char m[160];snprintf(m,sizeof(m),"PROXY MODE: %s",proxy_mode_name(st.proxy.mode));safe_text(b,s,68,455,2,m,muted);rect(b,s,42,540,1196,62,panel);safe_text(b,s,68,558,2,"A CONNECT",white);safe_text(b,s,330,558,2,"ZR PROXY",muted);safe_text(b,s,650,558,2,"Y TEST",muted);safe_text(b,s,1080,558,2,"+ EXIT",white);}

static void draw_cover_panel(u32* b,const State& st,int x,int y,int w,int h){
    u32 card=rgba(24,27,37),edge=rgba(61,66,82),white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126);
    rect(b,g_stride,x,y,w,h,card);border(b,g_stride,x,y,w,h,2,edge);
    if(st.cover.valid()){
        cover_draw_fit(b,g_stride,W,H,st.cover,x+12,y+12,w-24,h-92);
    }else{
        text(b,x+32,y+h/2,20,"暂无封面",muted,w-64);
    }
    if(!st.view.empty()){
        const auto& it=st.items[st.view[st.selected]];
        text(b,x+18,y+h-48,18,it.title.empty()?it.id:it.title,white,w-36,1);
        text(b,x+18,y+h-20,14,it.id,pink,w-36,1);
    }
}

static void draw_catalog(u32* b,const State& st){
    header(b,"首页");
    u32 white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126),card=rgba(24,27,37),sel=rgba(37,40,53);
    text(b,40,132,25,"动漫目录",white,300);
    text(b,40,165,17,st.search.empty()?"GiriGiri 公共目录 · HOS 系统字体 · 封面缓存":("搜索："+st.search),muted,820);
    if(st.view.empty()){
        rect(b,g_stride,40,205,820,360,card);text(b,75,285,28,"没有可显示的番剧",white,700);draw_cover_panel(b,890,190,350,390);footer(b,st,"Y 刷新   + 退出");return;
    }
    int rows=7,start=st.selected>=rows?st.selected-rows+1:0;
    for(int r=0;r<rows;++r){
        int vi=start+r;if(vi>=(int)st.view.size())break;const auto& it=st.items[st.view[vi]];int y=190+r*62;bool on=vi==st.selected;
        rect(b,g_stride,40,y,820,54,on?sel:card);if(on)rect(b,g_stride,40,y,6,54,pink);
        std::string title=it.title.empty()?it.id:it.title;text(b,66,y+36,20,title,white,650);text(b,735,y+34,14,it.id,on?pink:muted,110);
    }
    draw_cover_panel(b,st,890,190,350,390);
    char c[64];snprintf(c,sizeof(c),"%d / %zu",st.selected+1,st.view.size());text(b,760,615,15,c,muted,100);
    footer(b,st,"A 详情  X 搜索  Y 刷新");
}

static void draw_detail(u32* b,const State& st){
    header(b,"详情");
    u32 white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126),card=rgba(24,27,37),sel=rgba(37,40,53),edge=rgba(61,66,82);
    text(b,40,135,29,st.detail.title,white,1170);text(b,40,170,17,st.detail.status.empty()?st.detail.id:(st.detail.status+" · "+st.detail.id),pink,1000);
    rect(b,g_stride,40,195,220,320,card);border(b,g_stride,40,195,220,320,2,edge);
    if(st.cover.valid())cover_draw_fit(b,g_stride,W,H,st.cover,50,205,200,300);else text(b,85,360,19,"暂无封面",muted,150);
    rect(b,g_stride,285,195,935,125,card);text(b,307,228,17,st.detail.description.empty()?"暂无简介":st.detail.description,muted,890,4);
    text(b,285,360,21,"选集",white,200);
    if(st.detail.episodes.empty())text(b,285,410,19,"没有解析到公开集数入口。",muted,760);
    else{
        const int cols=6;int start=(st.ep/18)*18;
        for(int i=0;i<18;++i){int ei=start+i;if(ei>=(int)st.detail.episodes.size())break;int x=285+(i%cols)*156,y=388+(i/cols)*70;bool on=ei==st.ep;rect(b,g_stride,x,y,142,58,on?sel:card);border(b,g_stride,x,y,142,58,on?3:1,on?pink:edge);text(b,x+12,y+37,16,st.detail.episodes[ei].label,on?white:muted,116);}
    }
    footer(b,st,"A 选择   B 返回");
}

static void draw_episode(u32* b,const State& st){header(b,"播放");u32 white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126),card=rgba(24,27,37);std::string label="集数",url;if(!st.detail.episodes.empty()&&st.ep<(int)st.detail.episodes.size()){label=st.detail.episodes[st.ep].label;url=st.detail.episodes[st.ep].url;}text(b,40,145,29,st.detail.title+" · "+label,white,1160);rect(b,g_stride,40,190,1200,330,card);text(b,70,245,23,"播放器接口",pink,500);text(b,70,295,18,"当前版本完成目录、标题、封面、详情与选集。播放核心下一阶段接入。",muted,1100,2);text(b,70,405,16,url,muted,1100,2);footer(b,st,"B 返回");}
static void draw_frame(Framebuffer& fb,const State& st){u32 stride=0;u32* b=(u32*)framebufferBegin(&fb,&stride);g_stride=stride/4;if(st.screen==SAFE)draw_safe(b,g_stride,st);else if(st.screen==CATALOG)draw_catalog(b,st);else if(st.screen==DETAIL)draw_detail(b,st);else draw_episode(b,st);framebufferEnd(&fb);}

static void ensure_network(State& st){if(!st.socket_ok){Result rc=socketInitializeDefault();if(R_FAILED(rc)){char t[96];snprintf(t,sizeof(t),"SOCKET INIT FAILED 0X%08X",rc);st.status=t;return;}st.socket_ok=true;}if(!st.provider_ok){std::string m;if(!provider_init(m)){st.status="PROVIDER INIT FAILED: "+m;return;}st.provider_ok=true;}}
static bool unresolved(const AnimeItem& it){return it.title.empty()||it.title==it.id;}
static bool resolve_metadata(State& st,size_t idx){
    if(idx>=st.items.size())return false;
    if(!unresolved(st.items[idx])&&!st.items[idx].cover_url.empty())return true;
    AnimeDetail d;std::string m;
    if(provider_fetch_detail(st.proxy,st.items[idx],d,m)){
        if(!d.title.empty())st.items[idx].title=d.title;
        if(!d.cover_url.empty())st.items[idx].cover_url=d.cover_url;
        return true;
    }
    return false;
}
static void resolve_visible(State& st,Framebuffer& fb){
    if(st.view.empty())return;int rows=7,start=st.selected>=rows?st.selected-rows+1:0,end=std::min((int)st.view.size(),start+rows);
    for(int vi=start;vi<end;++vi){size_t idx=st.view[vi];if(!unresolved(st.items[idx])&&!st.items[idx].cover_url.empty())continue;char p[96];snprintf(p,sizeof(p),"正在读取番剧资料 %d/%d",vi-start+1,end-start);st.status=p;draw_frame(fb,st);resolve_metadata(st,idx);}
    rebuild(st);st.status="番剧资料缓存完成";
}
static void ensure_current_cover(State& st,Framebuffer& fb){
    if(st.view.empty())return;size_t idx=st.view[st.selected];AnimeItem& item=st.items[idx];
    if(st.cover_id==item.id&&st.cover.valid())return;
    st.cover.clear();st.cover_id.clear();
    if(item.cover_url.empty())resolve_metadata(st,idx);
    if(item.cover_url.empty()){st.status="该番剧暂未解析到封面";return;}
    st.status="正在加载封面...";draw_frame(fb,st);
    CoverImage img;std::string m;
    if(cover_load_cached_or_download(st.proxy,item.id,item.cover_url,img,m)){st.cover=std::move(img);st.cover_id=item.id;st.status=m;}else{st.status=m;}
}
static void connect_source(State& st,Framebuffer& fb){ensure_network(st);if(!st.socket_ok||!st.provider_ok)return;std::vector<AnimeItem> out;std::string m;if(!provider_fetch_home(st.proxy,out,m)){st.status=m;return;}st.items.swap(out);st.connected=true;st.status=m;rebuild(st);std::string e;if(!init_hos_fonts(e)){st.status=m+" · HOS FONT FAILED: "+e;return;}st.screen=CATALOG;draw_frame(fb,st);resolve_visible(st,fb);ensure_current_cover(st,fb);if(st.status.find("失败")==std::string::npos&&st.status.find("暂无")==std::string::npos)st.status=m;}
static void refresh(State& st,Framebuffer& fb){std::vector<AnimeItem> out;std::string m;if(provider_fetch_home(st.proxy,out,m)){st.items.swap(out);st.selected=0;st.cover.clear();st.cover_id.clear();rebuild(st);resolve_visible(st,fb);ensure_current_cover(st,fb);if(st.status.find("失败")==std::string::npos)st.status=m;}else st.status=m;}
static void open_detail(State& st,Framebuffer& fb){if(st.view.empty())return;size_t idx=st.view[st.selected];AnimeDetail d;std::string m;if(provider_fetch_detail(st.proxy,st.items[idx],d,m)){st.detail=std::move(d);if(!st.detail.title.empty())st.items[idx].title=st.detail.title;if(!st.detail.cover_url.empty())st.items[idx].cover_url=st.detail.cover_url;st.ep=0;ensure_current_cover(st,fb);st.screen=DETAIL;}st.status=m;}
static void do_search(State& st){std::string q=st.search;if(!keyboard("搜索番剧","搜索已缓存的番剧标题；留空显示全部",q,q))return;st.search=q;st.selected=0;st.cover.clear();st.cover_id.clear();rebuild(st);st.status=q.empty()?"已清除搜索":("搜索："+q);}

int main(int argc,char** argv){
    (void)argc;(void)argv;mkdir(LOG_DIR,0777);FILE* lf=fopen(LOG_PATH,"w");if(lf){fprintf(lf,"NXAnime 0.3 startup\n");fclose(lf);}log_stage("main entered");
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);
    Framebuffer fb;Result frc=framebufferCreate(&fb,nwindowGetDefault(),W,H,PIXEL_FORMAT_RGBA_8888,2);if(R_FAILED(frc))return (int)frc;framebufferMakeLinear(&fb);
    State st;proxy_config_load(&st.proxy);draw_frame(fb,st);
    while(appletMainLoop()){
        padUpdate(&pad);u64 d=padGetButtonsDown(&pad);if(d&HidNpadButton_Plus)break;
        if(st.screen==SAFE){
            if(d&HidNpadButton_A){st.status="CONNECTING GIRIGIRI...";draw_frame(fb,st);connect_source(st,fb);}
            if(d&HidNpadButton_Y){ensure_network(st);if(st.socket_ok&&st.provider_ok){std::string m;bool ok=provider_test_source(st.proxy,m);st.status=(ok?"SOURCE TEST OK: ":"SOURCE TEST FAILED: ")+m;}}
            if(d&HidNpadButton_ZR){st.proxy.mode=(ProxyMode)(((int)st.proxy.mode+1)%3);proxy_config_save(&st.proxy);st.status=std::string("PROXY MODE: ")+proxy_mode_name(st.proxy.mode);}
        }else if(st.screen==CATALOG){
            if(!st.view.empty()){
                int old=st.selected;if(d&HidNpadButton_Up)st.selected=(st.selected-1+(int)st.view.size())%(int)st.view.size();if(d&HidNpadButton_Down)st.selected=(st.selected+1)%(int)st.view.size();
                if(st.selected!=old){resolve_visible(st,fb);ensure_current_cover(st,fb);}
                if(d&HidNpadButton_A)open_detail(st,fb);
            }
            if(d&HidNpadButton_X){do_search(st);resolve_visible(st,fb);ensure_current_cover(st,fb);}
            if(d&HidNpadButton_Y)refresh(st,fb);
        }else if(st.screen==DETAIL){
            int n=st.detail.episodes.size();if(n>0){if(d&HidNpadButton_Left)st.ep=(st.ep-1+n)%n;if(d&HidNpadButton_Right)st.ep=(st.ep+1)%n;if(d&HidNpadButton_Up)st.ep=std::max(0,st.ep-6);if(d&HidNpadButton_Down)st.ep=std::min(n-1,st.ep+6);if(d&HidNpadButton_A)st.screen=EPISODE;}if(d&HidNpadButton_B)st.screen=CATALOG;
        }else if(st.screen==EPISODE){if(d&HidNpadButton_B)st.screen=DETAIL;}
        draw_frame(fb,st);svcSleepThread(12'000'000);
    }
    st.cover.clear();if(st.provider_ok)provider_exit();if(st.socket_ok)socketExit();shutdown_fonts();framebufferClose(&fb);return 0;
}
