from pathlib import Path

provider = Path('nxanime_source/provider.cpp')
main = Path('nxanime_source/main.cpp')

p = provider.read_text(encoding='utf-8')
m = main.read_text(encoding='utf-8')

# Add UTF-8 percent encoding for the website's own search endpoint.
marker = r'''static bool looks_like_anime_path(const std::string& href) {
'''
insert = r'''static std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

static bool looks_like_anime_path(const std::string& href) {
'''
if marker not in p:
    raise SystemExit('url encode insertion marker not found')
p = p.replace(marker, insert, 1)

# Add real server-side search. GiriGiri uses /search/-------------/?wd=<keyword>.
marker2 = r'''bool provider_fetch_detail(const ProxyConfig& proxy,
'''
search_impl = r'''bool provider_search(const ProxyConfig& proxy,
                     const std::string& query,
                     std::vector<AnimeItem>& out,
                     std::string& status) {
    if (query.empty()) return provider_fetch_home(proxy, out, status);

    std::string html;
    std::string url = std::string(BASE_URL) + "/search/-------------/?wd=" + url_encode(query);
    if (!http_get(url, proxy, html, status)) return false;

    parse_home(html, out);
    if (out.empty()) {
        status = "没有搜索到相关番剧";
        return true;
    }

    char tmp[128];
    std::snprintf(tmp, sizeof(tmp), "搜索完成：%zu 个结果", out.size());
    status = tmp;
    return true;
}

bool provider_fetch_detail(const ProxyConfig& proxy,
'''
if marker2 not in p:
    raise SystemExit('provider search insertion marker not found')
p = p.replace(marker2, search_impl, 1)
p = p.replace('NXAnime/0.3.1 NintendoSwitch', 'NXAnime/0.3.2 NintendoSwitch')
provider.write_text(p, encoding='utf-8')

# Default preset is QWERTY. Force the HOS Simplified Chinese keyboard for anime search.
old_keyboard = r'''static bool keyboard(const char* h,const char* g,const std::string& initial,std::string& out){SwkbdConfig k{};if(R_FAILED(swkbdCreate(&k,0)))return false;swkbdConfigMakePresetDefault(&k);swkbdConfigSetHeaderText(&k,h);swkbdConfigSetGuideText(&k,g);swkbdConfigSetInitialText(&k,initial.c_str());char b[768]={0};Result rc=swkbdShow(&k,b,sizeof(b));swkbdClose(&k);if(R_FAILED(rc))return false;out=b;return true;}
'''
new_keyboard = r'''static bool keyboard(const char* h,const char* g,const std::string& initial,std::string& out){SwkbdConfig k{};if(R_FAILED(swkbdCreate(&k,0)))return false;swkbdConfigMakePresetDefault(&k);swkbdConfigSetType(&k,SwkbdType_ZhHans);swkbdConfigSetHeaderText(&k,h);swkbdConfigSetGuideText(&k,g);swkbdConfigSetInitialText(&k,initial.c_str());char b[768]={0};Result rc=swkbdShow(&k,b,sizeof(b));swkbdClose(&k);if(R_FAILED(rc))return false;out=b;return true;}
'''
if old_keyboard not in m:
    raise SystemExit('keyboard block not found')
m = m.replace(old_keyboard, new_keyboard, 1)

# Replace local filtering with the site's own search endpoint.
old_search = r'''static void do_search(State& st){std::string q=st.search;if(!keyboard("搜索番剧","搜索已缓存的番剧标题；留空显示全部",q,q))return;st.search=q;st.selected=0;st.cover.clear();st.cover_id.clear();rebuild(st);st.status=q.empty()?"已清除搜索":("搜索："+q);}
'''
new_search = r'''static void do_search(State& st,Framebuffer& fb){
    std::string q=st.search;
    if(!keyboard("搜索番剧","输入中文片名进行 GiriGiri 站内搜索；留空返回首页",q,q))return;

    st.search=q;
    st.selected=0;
    st.cover.clear();
    st.cover_id.clear();

    std::vector<AnimeItem> out;
    std::string msg;
    st.status=q.empty()?"正在返回首页...":("正在搜索："+q);
    draw_frame(fb,st);

    bool ok=q.empty()?provider_fetch_home(st.proxy,out,msg):provider_search(st.proxy,q,out,msg);
    if(!ok){st.status=msg;return;}

    st.items.swap(out);
    rebuild(st);
    st.status=msg;
    if(!st.view.empty()){
        resolve_visible(st,fb);
        ensure_current_cover(st,fb);
        st.status=msg;
    }
}
'''
if old_search not in m:
    raise SystemExit('do_search block not found')
m = m.replace(old_search, new_search, 1)

old_call = r'''if(d&HidNpadButton_X){do_search(st);resolve_visible(st,fb);ensure_current_cover(st,fb);}'''
new_call = r'''if(d&HidNpadButton_X)do_search(st,fb);'''
if old_call not in m:
    raise SystemExit('search call not found')
m = m.replace(old_call, new_call, 1)

m = m.replace('page+" · v0.3.1"', 'page+" · v0.3.2"')
m = m.replace('"COVER BUILD 0.3.1"', '"SEARCH BUILD 0.3.2"')
m = m.replace('NXAnime 0.3.1 startup', 'NXAnime 0.3.2 startup')
main.write_text(m, encoding='utf-8')

print('v0.3.2 Chinese keyboard + remote search patch applied')
