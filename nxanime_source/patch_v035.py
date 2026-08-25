from pathlib import Path

main = Path('nxanime_source/main.cpp')
m = main.read_text(encoding='utf-8')

# 1) Add a dedicated SEARCH screen so X can always return from the search window.
old_enum = 'enum Screen{SAFE=0,CATALOG,DETAIL,EPISODE,FILTER};'
new_enum = 'enum Screen{SAFE=0,CATALOG,DETAIL,EPISODE,FILTER,SEARCH};'
if old_enum not in m:
    raise SystemExit('screen enum not found')
m = m.replace(old_enum, new_enum, 1)

# 2) Server search/filter already determine the result set. Do not run a second local title filter,
#    because fresh results may temporarily have GVxxxxx titles until metadata is resolved.
old_rebuild = r'''static void rebuild(State& st){st.view.clear();std::string q=lower_ascii(st.search);for(size_t i=0;i<st.items.size();++i){if(q.empty()||lower_ascii(st.items[i].title).find(q)!=std::string::npos||st.items[i].id.find(st.search)!=std::string::npos)st.view.push_back(i);}if(st.selected>=(int)st.view.size())st.selected=std::max(0,(int)st.view.size()-1);}'''
new_rebuild = r'''static void rebuild(State& st){st.view.clear();for(size_t i=0;i<st.items.size();++i)st.view.push_back(i);if(st.selected>=(int)st.view.size())st.selected=std::max(0,(int)st.view.size()-1);}'''
if old_rebuild not in m:
    raise SystemExit('rebuild block not found')
m = m.replace(old_rebuild, new_rebuild, 1)

# 3) Draw a dedicated search page. X/B return without changing results; A opens HOS Chinese keyboard.
marker = r'''static void draw_filter(u32* b,const State& st){'''
search_draw = r'''static void draw_search(u32* b,const State& st){
    header(b,"搜索");
    u32 white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126),card=rgba(24,27,37),edge=rgba(61,66,82);
    text(b,40,132,28,"搜索番剧",white,320);
    text(b,40,166,16,"使用 GiriGiri 站内搜索 · HOS 简体中文键盘",muted,760);
    rect(b,g_stride,70,215,1140,120,card);border(b,g_stride,70,215,1140,120,2,edge);
    text(b,95,255,17,"当前关键词",muted,220);
    text(b,95,305,25,st.search.empty()?"（未输入）":st.search,white,1030,1);
    rect(b,g_stride,70,385,340,58,card);border(b,g_stride,70,385,340,58,2,pink);text(b,105,423,19,"A 输入 / 搜索",white,240);
    rect(b,g_stride,455,385,300,58,card);text(b,495,423,19,"X 返回",white,200);
    rect(b,g_stride,800,385,330,58,card);text(b,840,423,19,"Y 清空并回首页",white,260);
    text(b,70,505,16,"提示：系统中文键盘内部仍使用 HOS 自己的取消键；返回 NXAnime 搜索页后可按 X 退出。",muted,1130,2);
    footer(b,st,"A 输入   X 返回   Y 首页");
}

static void draw_filter(u32* b,const State& st){'''
if marker not in m:
    raise SystemExit('draw_filter marker not found')
m = m.replace(marker, search_draw, 1)

old_draw_frame = r'''else if(st.screen==FILTER)draw_filter(b,st);else draw_episode(b,st);'''
new_draw_frame = r'''else if(st.screen==FILTER)draw_filter(b,st);else if(st.screen==SEARCH)draw_search(b,st);else draw_episode(b,st);'''
if old_draw_frame not in m:
    raise SystemExit('draw_frame filter branch not found')
m = m.replace(old_draw_frame, new_draw_frame, 1)

# 4) Add explicit home-return helper and make refresh clear stale search state.
old_refresh = r'''static void refresh(State& st,Framebuffer& fb){std::vector<AnimeItem> out;std::string m;if(provider_fetch_home(st.proxy,out,m)){st.items.swap(out);st.selected=0;st.cover.clear();st.cover_id.clear();rebuild(st);resolve_visible(st,fb);ensure_current_cover(st,fb);if(st.status.find("失败")==std::string::npos)st.status=m;}else st.status=m;}'''
new_refresh = r'''static void refresh(State& st,Framebuffer& fb){std::vector<AnimeItem> out;std::string m;if(provider_fetch_home(st.proxy,out,m)){st.items.swap(out);st.search.clear();st.selected=0;st.cover.clear();st.cover_id.clear();rebuild(st);resolve_visible(st,fb);ensure_current_cover(st,fb);if(st.status.find("失败")==std::string::npos)st.status=m;}else st.status=m;}
static void return_home(State& st,Framebuffer& fb){st.search.clear();refresh(st,fb);st.screen=CATALOG;}'''
if old_refresh not in m:
    raise SystemExit('refresh block not found')
m = m.replace(old_refresh, new_refresh, 1)

# 5) Replace immediate-search function with search-window execution helper.
start = m.find('static void do_search(State& st,Framebuffer& fb){')
if start < 0:
    raise SystemExit('do_search start not found')
end = m.find('\nstatic bool hit_box', start)
if end < 0:
    raise SystemExit('do_search end marker not found')
old_search_block = m[start:end]
new_search_block = r'''static void execute_search(State& st,Framebuffer& fb){
    std::string q=st.search;
    if(!keyboard("搜索番剧","输入中文片名；取消会回到 NXAnime 搜索页",q,q)){
        st.status="已取消输入";
        st.screen=SEARCH;
        return;
    }
    st.search=q;
    st.selected=0;
    st.cover.clear();
    st.cover_id.clear();
    if(q.empty()){
        return_home(st,fb);
        return;
    }
    std::vector<AnimeItem> out;
    std::string msg;
    st.status="正在搜索："+q;
    draw_frame(fb,st);
    if(!provider_search(st.proxy,q,out,msg)){st.status=msg;st.screen=SEARCH;return;}
    st.items.swap(out);
    rebuild(st);
    st.status=msg;
    st.screen=CATALOG;
    if(!st.view.empty()){
        resolve_visible(st,fb);
        ensure_current_cover(st,fb);
        st.status=msg;
    }
}
'''
m = m[:start] + new_search_block + m[end:]

# 6) Touch: add SEARCH screen actions before FILTER handling.
marker_touch = r'''    if(st.screen==FILTER){'''
insert_touch = r'''    if(st.screen==SEARCH){
        if(hit_box(tx,ty,70,385,340,58)){execute_search(st,fb);return;}
        if(hit_box(tx,ty,455,385,300,58)){st.screen=CATALOG;return;}
        if(hit_box(tx,ty,800,385,330,58)){return_home(st,fb);return;}
        return;
    }

    if(st.screen==FILTER){'''
if marker_touch not in m:
    raise SystemExit('touch filter marker not found')
m = m.replace(marker_touch, insert_touch, 1)

# 7) Controller behavior: X opens search window, B returns from search results to homepage.
old_catalog = r'''            if(d&HidNpadButton_X)do_search(st,fb);
            if(d&HidNpadButton_Y)refresh(st,fb);
            if(d&HidNpadButton_R)st.screen=FILTER;
        }else if(st.screen==FILTER){'''
new_catalog = r'''            if(d&HidNpadButton_X)st.screen=SEARCH;
            if(d&HidNpadButton_Y)refresh(st,fb);
            if(d&HidNpadButton_R)st.screen=FILTER;
            if((d&HidNpadButton_B)&&!st.search.empty())return_home(st,fb);
        }else if(st.screen==SEARCH){
            if(d&HidNpadButton_A)execute_search(st,fb);
            if(d&HidNpadButton_X)st.screen=CATALOG;
            if(d&HidNpadButton_B)st.screen=CATALOG;
            if(d&HidNpadButton_Y)return_home(st,fb);
        }else if(st.screen==FILTER){'''
if old_catalog not in m:
    raise SystemExit('catalog input block not found')
m = m.replace(old_catalog, new_catalog, 1)

# Catalog filter/search label should not say empty when server results exist.
m = m.replace('page+" · v0.3.4"', 'page+" · v0.3.5"')
m = m.replace('"FILTER BUILD 0.3.4"', '"SEARCH FIX 0.3.5"')
m = m.replace('NXAnime 0.3.4 startup', 'NXAnime 0.3.5 startup')
main.write_text(m, encoding='utf-8')

print('v0.3.5 search return + result visibility fixes applied')
