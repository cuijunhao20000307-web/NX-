from pathlib import Path

provider = Path('nxanime_source/provider.cpp')
main = Path('nxanime_source/main.cpp')

p = provider.read_text(encoding='utf-8')
m = main.read_text(encoding='utf-8')

# ---- Provider: server-side GiriGiri filter route ---------------------------------
marker = r'''bool provider_fetch_detail(const ProxyConfig& proxy,
'''
impl = r'''bool provider_filter(const ProxyConfig& proxy,
                     int channel_id,
                     const std::string& genre,
                     const std::string& quarter,
                     const std::string& year,
                     const std::string& language,
                     const std::string& sort_by,
                     std::vector<AnimeItem>& out,
                     std::string& status) {
    // GiriGiri/MacCMS filter route contains 12 hyphen-separated fields:
    // channel-quarter-sort-genre-language-letter-...-page-source-startYear-year.
    // We only fill the public filter dimensions used by NXAnime.
    std::string fields[12] = {
        std::to_string(channel_id), quarter, sort_by, genre, language,
        "", "", "", "", "", "", year
    };

    std::string url = std::string(BASE_URL) + "/show/";
    for (int i = 0; i < 12; ++i) {
        if (i) url.push_back('-');
        url += url_encode(fields[i]);
    }
    url += "/";

    std::string html;
    if (!http_get(url, proxy, html, status)) return false;
    parse_home(html, out);
    if (out.empty()) {
        status = "当前筛选没有结果";
        return true;
    }

    char tmp[128];
    std::snprintf(tmp, sizeof(tmp), "筛选完成：%zu 个结果", out.size());
    status = tmp;
    return true;
}

bool provider_fetch_detail(const ProxyConfig& proxy,
'''
if marker not in p:
    raise SystemExit('provider detail marker not found')
p = p.replace(marker, impl, 1)
p = p.replace('NXAnime/0.3.2 NintendoSwitch', 'NXAnime/0.3.4 NintendoSwitch')
provider.write_text(p, encoding='utf-8')

# ---- UI state --------------------------------------------------------------------
old_enum = 'enum Screen{SAFE=0,CATALOG,DETAIL,EPISODE};'
new_enum = 'enum Screen{SAFE=0,CATALOG,DETAIL,EPISODE,FILTER};'
if old_enum not in m:
    raise SystemExit('screen enum not found')
m = m.replace(old_enum, new_enum, 1)

old_state = r'''    int selected=0,ep=0;
    std::string search,status="READY - PRESS A TO CONNECT";
'''
new_state = r'''    int selected=0,ep=0;
    int filter_row=0;
    int f_channel=0,f_genre=0,f_quarter=0,f_year=0,f_lang=0,f_sort=0;
    std::string search,status="READY - PRESS A TO CONNECT";
'''
if old_state not in m:
    raise SystemExit('state insertion point not found')
m = m.replace(old_state, new_state, 1)

# ---- Filter option tables + UI ----------------------------------------------------
insert_before = r'''static void header(u32* b,const std::string& page){'''
filter_helpers = r'''static const char* FILTER_CHANNELS[] = {"日番","美番","剧场版"};
static const int FILTER_CHANNEL_IDS[] = {2,3,21};
static const char* FILTER_GENRES[] = {"全部","喜剧","爱情","动作","科幻","剧情","奇幻","冒险","悬疑","校园","后宫","热血","运动","百合","日常","异世界","音乐","萌"};
static const char* FILTER_QUARTERS[] = {"全部","一月","四月","七月","十月"};
static const char* FILTER_YEARS[] = {"全部","2026","2025","2024","2023","2022","2021","2020","2019","2018","2017","2016","2015"};
static const char* FILTER_LANGS[] = {"全部","日语","国语","英语","中文"};
static const char* FILTER_SORTS[] = {"最新","最热","评分"};
static const char* FILTER_SORT_VALUES[] = {"time","hits","score"};
static const char* FILTER_ROW_NAMES[] = {"频道","类型","季度","年份","语言","排序"};

static int wrapi(int v,int n){if(n<=0)return 0;v%=n;if(v<0)v+=n;return v;}
static int filter_count(int row){
    switch(row){
        case 0:return (int)(sizeof(FILTER_CHANNELS)/sizeof(FILTER_CHANNELS[0]));
        case 1:return (int)(sizeof(FILTER_GENRES)/sizeof(FILTER_GENRES[0]));
        case 2:return (int)(sizeof(FILTER_QUARTERS)/sizeof(FILTER_QUARTERS[0]));
        case 3:return (int)(sizeof(FILTER_YEARS)/sizeof(FILTER_YEARS[0]));
        case 4:return (int)(sizeof(FILTER_LANGS)/sizeof(FILTER_LANGS[0]));
        default:return (int)(sizeof(FILTER_SORTS)/sizeof(FILTER_SORTS[0]));
    }
}
static int* filter_value_ptr(State& st,int row){
    switch(row){
        case 0:return &st.f_channel;
        case 1:return &st.f_genre;
        case 2:return &st.f_quarter;
        case 3:return &st.f_year;
        case 4:return &st.f_lang;
        default:return &st.f_sort;
    }
}
static const char* filter_value_text(const State& st,int row){
    switch(row){
        case 0:return FILTER_CHANNELS[st.f_channel];
        case 1:return FILTER_GENRES[st.f_genre];
        case 2:return FILTER_QUARTERS[st.f_quarter];
        case 3:return FILTER_YEARS[st.f_year];
        case 4:return FILTER_LANGS[st.f_lang];
        default:return FILTER_SORTS[st.f_sort];
    }
}
static void filter_cycle(State& st,int dir){int* v=filter_value_ptr(st,st.filter_row);*v=wrapi(*v+dir,filter_count(st.filter_row));}
static void filter_reset(State& st){st.f_channel=0;st.f_genre=0;st.f_quarter=0;st.f_year=0;st.f_lang=0;st.f_sort=0;st.filter_row=0;st.status="筛选已重置";}

static void header(u32* b,const std::string& page){'''
if insert_before not in m:
    raise SystemExit('header marker not found')
m = m.replace(insert_before, filter_helpers, 1)

# Catalog: add filter chip and footer hint.
old_catalog_sub = r'''    text(b,40,165,17,st.search.empty()?"GiriGiri 公共目录 · HOS 系统字体 · 封面缓存":("搜索："+st.search),muted,820);
'''
new_catalog_sub = r'''    text(b,40,165,17,st.search.empty()?"GiriGiri 公共目录 · HOS 系统字体 · 封面缓存":("搜索："+st.search),muted,820);
    rect(b,g_stride,1055,112,165,46,card);border(b,g_stride,1055,112,165,46,2,pink);text(b,1100,143,18,"R 筛选",white,100);
'''
if old_catalog_sub not in m:
    raise SystemExit('catalog subtitle not found')
m = m.replace(old_catalog_sub, new_catalog_sub, 1)
m = m.replace('footer(b,st,"A 详情  X 搜索  Y 刷新");', 'footer(b,st,"A 详情  X 搜索  R 筛选");', 1)

# Draw filter page before draw_episode.
marker_draw = r'''static void draw_episode(u32* b,const State& st){'''
draw_filter = r'''static void draw_filter(u32* b,const State& st){
    header(b,"筛选");
    u32 white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126),card=rgba(24,27,37),sel=rgba(37,40,53),edge=rgba(61,66,82);
    text(b,40,132,27,"番剧筛选",white,300);
    text(b,40,162,16,"直接使用 GiriGiri 公开筛选页面 · 左右切换选项",muted,760);
    for(int row=0;row<6;++row){
        int y=190+row*64;bool on=row==st.filter_row;
        rect(b,g_stride,70,y,1140,52,on?sel:card);border(b,g_stride,70,y,1140,52,on?3:1,on?pink:edge);
        text(b,95,y+34,19,FILTER_ROW_NAMES[row],on?pink:white,180);
        text(b,380,y+34,19,"‹",muted,40);
        text(b,465,y+34,20,filter_value_text(st,row),white,520);
        text(b,1120,y+34,19,"›",muted,40);
    }
    rect(b,g_stride,70,590,360,42,card);text(b,95,619,16,"X 应用筛选",white,220);
    rect(b,g_stride,460,590,300,42,card);text(b,485,619,16,"Y 重置",white,180);
    rect(b,g_stride,790,590,300,42,card);text(b,815,619,16,"B 返回",white,180);
    footer(b,st,"摇杆/十字键操作");
}

static void draw_episode(u32* b,const State& st){'''
if marker_draw not in m:
    raise SystemExit('draw episode marker not found')
m = m.replace(marker_draw, draw_filter, 1)

old_draw_frame = r'''static void draw_frame(Framebuffer& fb,const State& st){u32 stride=0;u32* b=(u32*)framebufferBegin(&fb,&stride);g_stride=stride/4;if(st.screen==SAFE)draw_safe(b,g_stride,st);else if(st.screen==CATALOG)draw_catalog(b,st);else if(st.screen==DETAIL)draw_detail(b,st);else draw_episode(b,st);framebufferEnd(&fb);}'''
new_draw_frame = r'''static void draw_frame(Framebuffer& fb,const State& st){u32 stride=0;u32* b=(u32*)framebufferBegin(&fb,&stride);g_stride=stride/4;if(st.screen==SAFE)draw_safe(b,g_stride,st);else if(st.screen==CATALOG)draw_catalog(b,st);else if(st.screen==DETAIL)draw_detail(b,st);else if(st.screen==FILTER)draw_filter(b,st);else draw_episode(b,st);framebufferEnd(&fb);}'''
if old_draw_frame not in m:
    raise SystemExit('draw frame block not found')
m = m.replace(old_draw_frame, new_draw_frame, 1)

# Apply filter helper before do_search.
marker_search = r'''static void do_search(State& st,Framebuffer& fb){'''
apply_helper = r'''static void apply_filter(State& st,Framebuffer& fb){
    std::vector<AnimeItem> out;std::string msg;
    const char* genre=st.f_genre?FILTER_GENRES[st.f_genre]:"";
    const char* quarter=st.f_quarter?FILTER_QUARTERS[st.f_quarter]:"";
    const char* year=st.f_year?FILTER_YEARS[st.f_year]:"";
    const char* lang=st.f_lang?FILTER_LANGS[st.f_lang]:"";
    const char* sortv=FILTER_SORT_VALUES[st.f_sort];
    st.status="正在应用筛选...";draw_frame(fb,st);
    if(!provider_filter(st.proxy,FILTER_CHANNEL_IDS[st.f_channel],genre,quarter,year,lang,sortv,out,msg)){st.status=msg;return;}
    st.items.swap(out);st.search.clear();st.selected=0;st.cover.clear();st.cover_id.clear();rebuild(st);st.status=msg;st.screen=CATALOG;
    if(!st.view.empty()){resolve_visible(st,fb);ensure_current_cover(st,fb);st.status=msg;}
}

static void do_search(State& st,Framebuffer& fb){'''
if marker_search not in m:
    raise SystemExit('do_search marker not found')
m = m.replace(marker_search, apply_helper, 1)

# Touch support: filter chip + filter page.
cat_touch_marker = r'''    if(st.screen==CATALOG){
'''
cat_touch_insert = r'''    if(st.screen==FILTER){
        if(ty>=190&&ty<574&&tx>=70&&tx<1210){
            int row=(int)(ty-190)/64;
            if(row>=0&&row<6){
                st.filter_row=row;
                filter_cycle(st,tx<640?-1:1);
            }
            return;
        }
        if(ty>=580&&ty<640){
            if(tx<440)apply_filter(st,fb);
            else if(tx<780)filter_reset(st);
            else st.screen=CATALOG;
        }
        return;
    }

    if(st.screen==CATALOG){
        if(hit_box(tx,ty,1055,105,170,58)){st.screen=FILTER;return;}
'''
if cat_touch_marker not in m:
    raise SystemExit('catalog touch marker not found')
m = m.replace(cat_touch_marker, cat_touch_insert, 1)

# Controller: R opens filter; add FILTER input branch.
old_catalog_input = r'''            if(d&HidNpadButton_X)do_search(st,fb);
            if(d&HidNpadButton_Y)refresh(st,fb);
        }else if(st.screen==DETAIL){
'''
new_catalog_input = r'''            if(d&HidNpadButton_X)do_search(st,fb);
            if(d&HidNpadButton_Y)refresh(st,fb);
            if(d&HidNpadButton_R)st.screen=FILTER;
        }else if(st.screen==FILTER){
            if(d&HidNpadButton_Up)st.filter_row=wrapi(st.filter_row-1,6);
            if(d&HidNpadButton_Down)st.filter_row=wrapi(st.filter_row+1,6);
            if(d&HidNpadButton_Left)filter_cycle(st,-1);
            if(d&HidNpadButton_Right)filter_cycle(st,1);
            if(d&HidNpadButton_A)filter_cycle(st,1);
            if(d&HidNpadButton_X)apply_filter(st,fb);
            if(d&HidNpadButton_Y)filter_reset(st);
            if(d&HidNpadButton_B)st.screen=CATALOG;
        }else if(st.screen==DETAIL){
'''
if old_catalog_input not in m:
    raise SystemExit('catalog input block not found')
m = m.replace(old_catalog_input, new_catalog_input, 1)

m = m.replace('page+" · v0.3.3"', 'page+" · v0.3.4"')
m = m.replace('"TOUCH BUILD 0.3.3"', '"FILTER BUILD 0.3.4"')
m = m.replace('NXAnime 0.3.3 startup', 'NXAnime 0.3.4 startup')
main.write_text(m, encoding='utf-8')

print('v0.3.4 filter patch applied')
