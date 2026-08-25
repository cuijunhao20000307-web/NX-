from pathlib import Path

provider = Path('nxanime_source/provider.cpp')
main = Path('nxanime_source/main.cpp')

p = provider.read_text(encoding='utf-8')
m = main.read_text(encoding='utf-8')

old_heading = r'''static std::string extract_heading(const std::string& html) {
    const char* tags[] = {"h1", "h2", "h3"};
    for (const char* name : tags) {
        std::string open = std::string("<") + name;
        std::string close = std::string("</") + name + ">";
        size_t h = html.find(open);
        if (h == std::string::npos) continue;
        size_t gt = html.find('>', h);
        size_t end = html.find(close, gt);
        if (gt == std::string::npos || end == std::string::npos) continue;
        std::string t = strip_tags(html.substr(gt + 1, end - gt - 1));
        if (!t.empty() && t.size() < 220) return t;
    }
    return {};
}
'''
new_heading = r'''static bool bad_title(const std::string& t) {
    if (t.empty()) return true;
    static const char* bad[] = {"影片参数", "影片參數", "番剧列表", "番劇列表", "详情", "詳情", "播放", "收藏"};
    for (const char* s : bad) if (t == s) return true;
    return false;
}

static std::string clean_document_title(std::string t) {
    t = trim(html_decode(t));
    const char* cuts[] = {"_日番", "_劇場版", "_剧场版", " - girigiri", "_girigiri", " | girigiri"};
    for (const char* s : cuts) {
        size_t p = t.find(s);
        if (p != std::string::npos && p > 0) t.resize(p);
    }
    return trim(t);
}

static std::string extract_document_title(const std::string& html) {
    size_t p = html.find("<title");
    if (p == std::string::npos) return {};
    size_t gt = html.find('>', p);
    size_t e = html.find("</title>", gt);
    if (gt == std::string::npos || e == std::string::npos) return {};
    std::string t = clean_document_title(strip_tags(html.substr(gt + 1, e - gt - 1)));
    return bad_title(t) ? std::string{} : t;
}

static std::string extract_heading(const std::string& html) {
    const char* tags[] = {"h1", "h2", "h3"};
    for (const char* name : tags) {
        std::string open = std::string("<") + name;
        std::string close = std::string("</") + name + ">";
        size_t pos = 0;
        while (true) {
            size_t h = html.find(open, pos);
            if (h == std::string::npos) break;
            size_t gt = html.find('>', h);
            size_t end = html.find(close, gt);
            if (gt == std::string::npos || end == std::string::npos) break;
            std::string t = trim(strip_tags(html.substr(gt + 1, end - gt - 1)));
            if (!bad_title(t) && t.size() < 220) return t;
            pos = end + close.size();
        }
    }
    return {};
}
'''
if old_heading not in p:
    raise SystemExit('heading block not found')
p = p.replace(old_heading, new_heading, 1)

old_cover = r'''static std::string extract_cover(const std::string& html) {
    std::string u = extract_meta_value(html, nullptr, "og:image");
    if (u.empty()) u = extract_meta_value(html, "twitter:image", nullptr);
    if (!u.empty()) return absolute_url(u);

    size_t pos = 0;
    while (true) {
        size_t ip = html.find("<img", pos);
        if (ip == std::string::npos) break;
        size_t ie = html.find('>', ip);
        if (ie == std::string::npos) break;
        std::string tag = html.substr(ip, ie - ip + 1);
        std::string alt = attr_value(tag, "alt");
        std::string cls = attr_value(tag, "class");
        std::string low = alt + " " + cls;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        if (low.find("poster") != std::string::npos || low.find("cover") != std::string::npos || alt == "海报图") {
            const char* attrs[] = {"data-src", "data-original", "src"};
            for (const char* a : attrs) {
                std::string src = attr_value(tag, a);
                if (!src.empty() && src.rfind("data:", 0) != 0) return absolute_url(src);
            }
        }
        pos = ie + 1;
    }
    return {};
}
'''
new_cover = r'''static bool looks_like_cover_url(const std::string& u) {
    if (u.empty() || u.rfind("data:", 0) == 0) return false;
    std::string low = u;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    if (low.find("/upload/vod/") != std::string::npos) return true;
    return low.find(".webp") != std::string::npos || low.find(".jpg") != std::string::npos ||
           low.find(".jpeg") != std::string::npos || low.find(".png") != std::string::npos;
}

static std::string extract_cover(const std::string& html) {
    std::string u = extract_meta_value(html, nullptr, "og:image");
    if (u.empty()) u = extract_meta_value(html, "twitter:image", nullptr);
    if (looks_like_cover_url(u)) return absolute_url(u);

    // GiriGiri detail pages expose the poster as /upload/vod/...webp.
    // Prefer that path regardless of alt/class because those attributes change between page revisions.
    size_t pos = 0;
    while (true) {
        size_t ip = html.find("<img", pos);
        if (ip == std::string::npos) break;
        size_t ie = html.find('>', ip);
        if (ie == std::string::npos) break;
        std::string tag = html.substr(ip, ie - ip + 1);
        const char* attrs[] = {"data-src", "data-original", "data-lazy-src", "src"};
        for (const char* a : attrs) {
            std::string src = attr_value(tag, a);
            if (src.find("/upload/vod/") != std::string::npos) return absolute_url(src);
        }
        pos = ie + 1;
    }

    // Raw-source fallback for lazy-loaded markup.
    size_t raw = html.find("/upload/vod/");
    if (raw != std::string::npos) {
        size_t end = raw;
        while (end < html.size()) {
            char c = html[end];
            if (c == '\"' || c == '\'' || c == '<' || c == '>' || std::isspace((unsigned char)c)) break;
            ++end;
        }
        if (end > raw) return absolute_url(html.substr(raw, end - raw));
    }

    // Last fallback: any plausible image tag, excluding obvious UI assets.
    pos = 0;
    while (true) {
        size_t ip = html.find("<img", pos);
        if (ip == std::string::npos) break;
        size_t ie = html.find('>', ip);
        if (ie == std::string::npos) break;
        std::string tag = html.substr(ip, ie - ip + 1);
        const char* attrs[] = {"data-src", "data-original", "data-lazy-src", "src"};
        for (const char* a : attrs) {
            std::string src = attr_value(tag, a);
            std::string low = src;
            std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c){ return (char)std::tolower(c); });
            if (looks_like_cover_url(src) && low.find("logo") == std::string::npos && low.find("avatar") == std::string::npos)
                return absolute_url(src);
        }
        pos = ie + 1;
    }
    return {};
}
'''
if old_cover not in p:
    raise SystemExit('cover block not found')
p = p.replace(old_cover, new_cover, 1)

old_detail = r'''    out.title = extract_heading(html);
    if (out.title.empty()) out.title = item.title;
'''
new_detail = r'''    // The first heading on current GiriGiri pages is often "影片参数"; do not use it as a title.
    out.title = extract_document_title(html);
    if (bad_title(out.title)) out.title = extract_heading(html);
    if (bad_title(out.title)) out.title = item.title;
'''
if old_detail not in p:
    raise SystemExit('detail title block not found')
p = p.replace(old_detail, new_detail, 1)
p = p.replace('NXAnime/0.3 NintendoSwitch', 'NXAnime/0.3.1 NintendoSwitch')
provider.write_text(p, encoding='utf-8')

old_unresolved = 'static bool unresolved(const AnimeItem& it){return it.title.empty()||it.title==it.id;}'
new_unresolved = 'static bool unresolved(const AnimeItem& it){return it.title.empty()||it.title==it.id||it.title=="影片参数"||it.title=="影片參數";}'
if old_unresolved not in m:
    raise SystemExit('unresolved block not found')
m = m.replace(old_unresolved, new_unresolved, 1)

old_resolve = r'''static void resolve_visible(State& st,Framebuffer& fb){
    if(st.view.empty())return;int rows=7,start=st.selected>=rows?st.selected-rows+1:0,end=std::min((int)st.view.size(),start+rows);
    for(int vi=start;vi<end;++vi){size_t idx=st.view[vi];if(!unresolved(st.items[idx])&&!st.items[idx].cover_url.empty())continue;char p[96];snprintf(p,sizeof(p),"正在读取番剧资料 %d/%d",vi-start+1,end-start);st.status=p;draw_frame(fb,st);resolve_metadata(st,idx);}
    rebuild(st);st.status="番剧资料缓存完成";
}
'''
new_resolve = r'''static void resolve_visible(State& st,Framebuffer& fb){
    // Performance: only resolve the currently selected item. The old code fetched all 7 visible
    // detail pages synchronously, which made D-pad movement feel sluggish on Switch.
    if(st.view.empty())return;
    size_t idx=st.view[st.selected];
    if(unresolved(st.items[idx])||st.items[idx].cover_url.empty()){
        st.status="正在读取当前番剧资料...";
        draw_frame(fb,st);
        resolve_metadata(st,idx);
        rebuild(st);
    }
}
'''
if old_resolve not in m:
    raise SystemExit('resolve_visible block not found')
m = m.replace(old_resolve, new_resolve, 1)
m = m.replace('page+" · v0.3"', 'page+" · v0.3.1"')
m = m.replace('"COVER BUILD 0.3"', '"COVER BUILD 0.3.1"')
m = m.replace('NXAnime 0.3 startup', 'NXAnime 0.3.1 startup')
main.write_text(m, encoding='utf-8')

print('v0.3.1 patch applied')
