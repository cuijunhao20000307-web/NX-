from pathlib import Path

main = Path('nxanime_source/main.cpp')
m = main.read_text(encoding='utf-8')

marker = r'''static void execute_search(State& st,Framebuffer& fb){'''
helpers = r'''static std::string search_norm(const std::string& s){
    std::string out;out.reserve(s.size());
    for(unsigned char c:s){
        if(c<128){
            if(std::isspace(c))continue;
            if(c=='-'||c=='_'||c=='.'||c==','||c==':'||c==';'||c=='/'||c=='\\'||c=='('||c==')'||c=='['||c==']')continue;
            out.push_back((char)std::tolower(c));
        }else out.push_back((char)c);
    }
    return out;
}

static int fuzzy_score(const std::string& title,const std::string& query){
    std::string t=search_norm(title),q=search_norm(query);
    if(q.empty()||t.empty())return 0;
    if(t==q)return 10000;
    if(t.rfind(q,0)==0)return 9000-(int)std::min<size_t>(999,t.size()-q.size());
    size_t p=t.find(q);
    if(p!=std::string::npos)return 8000-(int)std::min<size_t>(999,p);
    if(q.find(t)!=std::string::npos)return 7000-(int)std::min<size_t>(999,q.size()-t.size());

    // Loose ordered-character fallback. UTF-8 byte order is preserved, so Chinese partial titles
    // still receive a lower score when the same characters occur in sequence with gaps.
    size_t qi=0;
    for(size_t i=0;i<t.size()&&qi<q.size();++i)if(t[i]==q[qi])++qi;
    if(qi==q.size())return 4000-(int)std::min<size_t>(999,t.size()-q.size());
    return 0;
}

static void fuzzy_sort(std::vector<AnimeItem>& items,const std::string& query){
    std::stable_sort(items.begin(),items.end(),[&](const AnimeItem& a,const AnimeItem& b){
        int sa=fuzzy_score(a.title,query),sb=fuzzy_score(b.title,query);
        if(sa!=sb)return sa>sb;
        if(a.title.size()!=b.title.size())return a.title.size()<b.title.size();
        return a.id<b.id;
    });
}

static std::vector<std::string> utf8_prefixes(const std::string& q){
    std::vector<size_t> ends;
    for(size_t i=0;i<q.size();){
        unsigned char c=(unsigned char)q[i];size_t n=1;
        if((c&0xE0)==0xC0)n=2;else if((c&0xF0)==0xE0)n=3;else if((c&0xF8)==0xF0)n=4;
        i=std::min(q.size(),i+n);ends.push_back(i);
    }
    std::vector<std::string> out;
    if(ends.size()<=2)return out;
    // Try progressively shorter prefixes, but never go below two characters.
    for(size_t n=ends.size()-1;n>=2;--n){
        out.push_back(q.substr(0,ends[n-1]));
        if(n==2)break;
        if(out.size()>=3)break;
    }
    return out;
}

static void execute_search(State& st,Framebuffer& fb){'''
if marker not in m:
    raise SystemExit('execute_search marker not found')
m = m.replace(marker, helpers, 1)

old = r'''    if(!provider_search(st.proxy,q,out,msg)){st.status=msg;st.screen=SEARCH;return;}
    st.items.swap(out);rebuild(st);st.status=msg;st.screen=CATALOG;
    if(!st.view.empty()){resolve_visible(st,fb);ensure_current_cover(st,fb);st.status=msg;}
'''
new = r'''    if(!provider_search(st.proxy,q,out,msg)){st.status=msg;st.screen=SEARCH;return;}

    // If the site returns no result for a long title, retry a few shorter prefixes.
    // Example: a full season/subtitle query can fall back to its core title automatically.
    if(out.empty()){
        for(const std::string& shortq:utf8_prefixes(q)){
            std::vector<AnimeItem> retry;std::string retrymsg;
            st.status="模糊搜索："+shortq;draw_frame(fb,st);
            if(provider_search(st.proxy,shortq,retry,retrymsg)&&!retry.empty()){
                out.swap(retry);msg="模糊搜索「"+q+"」："+std::to_string(out.size())+" 个结果";break;
            }
        }
    }

    fuzzy_sort(out,q);
    st.items.swap(out);rebuild(st);st.status=msg;st.screen=CATALOG;
    if(!st.view.empty()){resolve_visible(st,fb);ensure_current_cover(st,fb);st.status=msg;}
'''
if old not in m:
    raise SystemExit('execute_search result block not found')
m = m.replace(old, new, 1)

# Search-page copy and version labels.
m = m.replace('使用 GiriGiri 站内搜索 · HOS 简体中文键盘', '部分关键词即可搜索 · 自动模糊匹配 · HOS 中文键盘')
m = m.replace('page+" · v0.3.5"', 'page+" · v0.3.6"')
m = m.replace('"SEARCH FIX 0.3.5"', '"FUZZY SEARCH 0.3.6"')
m = m.replace('NXAnime 0.3.5 startup', 'NXAnime 0.3.6 startup')

main.write_text(m, encoding='utf-8')
print('v0.3.6 fuzzy search patch applied')
