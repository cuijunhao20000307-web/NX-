from pathlib import Path

provider = Path('nxanime_source/provider.cpp')
main = Path('nxanime_source/main.cpp')
p = provider.read_text(encoding='utf-8')
m = main.read_text(encoding='utf-8')

# Replace the old HTML search page with GiriGiri's public MacCMS suggest endpoint.
# The normal /search/... page currently returns a system prompt to direct HTTP clients,
# while /index.php/ajax/suggest returns structured JSON and supports partial keywords.
start = p.find('bool provider_search(const ProxyConfig& proxy,')
end = p.find('\nbool provider_filter(', start)
if end < 0:
    end = p.find('\nbool provider_fetch_detail(', start)
if start < 0 or end < 0:
    raise SystemExit('provider_search block not found')

helpers = r'''static std::string json_unescape_simple(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c != '\\' || i + 1 >= s.size()) {
            out.push_back(c);
            continue;
        }
        char n = s[++i];
        switch (n) {
            case '/': out.push_back('/'); break;
            case '\\': out.push_back('\\'); break;
            case '"': out.push_back('"'); break;
            case 'n': out.push_back(' '); break;
            case 'r': out.push_back(' '); break;
            case 't': out.push_back(' '); break;
            default: out.push_back(n); break;
        }
    }
    return out;
}

static std::string json_string_field(const std::string& obj, const char* key) {
    std::string needle = std::string("\"") + key + "\":\"";
    size_t p = obj.find(needle);
    if (p == std::string::npos) return {};
    p += needle.size();
    std::string raw;
    bool esc = false;
    for (size_t i = p; i < obj.size(); ++i) {
        char c = obj[i];
        if (!esc && c == '"') break;
        if (!esc && c == '\\') {
            esc = true;
            raw.push_back(c);
            continue;
        }
        raw.push_back(c);
        esc = false;
    }
    return json_unescape_simple(raw);
}

static std::string json_number_field(const std::string& obj, const char* key) {
    std::string needle = std::string("\"") + key + "\":";
    size_t p = obj.find(needle);
    if (p == std::string::npos) return {};
    p += needle.size();
    while (p < obj.size() && std::isspace((unsigned char)obj[p])) ++p;
    size_t e = p;
    while (e < obj.size() && std::isdigit((unsigned char)obj[e])) ++e;
    return obj.substr(p, e - p);
}

bool provider_search(const ProxyConfig& proxy,
                     const std::string& query,
                     std::vector<AnimeItem>& out,
                     std::string& status) {
    out.clear();
    if (query.empty()) return provider_fetch_home(proxy, out, status);

    std::string body;
    std::string url = std::string(BASE_URL) + "/index.php/ajax/suggest?mid=1&wd=" + url_encode(query);
    if (!http_get(url, proxy, body, status)) return false;

    if (body.find("\"code\":1") == std::string::npos) {
        status = "搜索接口没有返回有效结果";
        return true;
    }

    size_t list_pos = body.find("\"list\":[");
    if (list_pos == std::string::npos) {
        status = "搜索结果格式异常";
        return true;
    }

    size_t pos = list_pos;
    std::set<std::string> seen;
    while (true) {
        size_t b = body.find("{\"id\":", pos);
        if (b == std::string::npos) break;
        size_t e = body.find('}', b);
        if (e == std::string::npos) break;
        std::string obj = body.substr(b, e - b + 1);
        pos = e + 1;

        std::string num = json_number_field(obj, "id");
        std::string name = json_string_field(obj, "name");
        std::string pic = json_string_field(obj, "pic");
        if (num.empty() || name.empty()) continue;

        std::string id = "GV" + num;
        if (!seen.insert(id).second) continue;

        AnimeItem item;
        item.id = id;
        item.title = name;
        item.url = std::string(BASE_URL) + "/" + id + "/";
        item.extra = "搜索结果";
        item.cover_url = absolute_url(pic);
        out.push_back(std::move(item));
        if (out.size() >= 40) break;
    }

    if (out.empty()) {
        status = "没有搜索到相关番剧";
        return true;
    }

    char tmp[128];
    std::snprintf(tmp, sizeof(tmp), "搜索完成：%zu 个结果", out.size());
    status = tmp;
    return true;
}
'''

p = p[:start] + helpers + p[end:]
p = p.replace('NXAnime/0.3.4 NintendoSwitch', 'NXAnime/0.3.7 NintendoSwitch')
provider.write_text(p, encoding='utf-8')

m = m.replace('page+" · v0.3.6"', 'page+" · v0.3.7"')
m = m.replace('"FUZZY SEARCH 0.3.6"', '"AJAX SEARCH 0.3.7"')
m = m.replace('NXAnime 0.3.6 startup', 'NXAnime 0.3.7 startup')
m = m.replace('部分关键词即可搜索 · 自动模糊匹配 · HOS 中文键盘', '部分关键词搜索 · AJAX 搜索 · HOS 中文键盘')
main.write_text(m, encoding='utf-8')

print('v0.3.7 AJAX suggest search patch applied')
