#include "provider.hpp"

#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>

static const char* BASE_URL = "https://ani.girigirilove.com";

static size_t write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    if (!userdata || !ptr) return 0;
    std::string* out = static_cast<std::string*>(userdata);
    size_t bytes = size * nmemb;
    out->append(static_cast<const char*>(ptr), bytes);
    return bytes;
}

static void apply_proxy(CURL* curl, const ProxyConfig& cfg) {
    if (!curl) return;
    if (cfg.mode == PROXY_MODE_DIRECT) {
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
        return;
    }

    if (!cfg.host[0] || cfg.port <= 0) return;

    char proxy_addr[384];
    std::snprintf(proxy_addr, sizeof(proxy_addr), "%s:%d", cfg.host, cfg.port);
    curl_easy_setopt(curl, CURLOPT_PROXY, proxy_addr);
    if (cfg.mode == PROXY_MODE_HTTP)
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
    else
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);

    if (cfg.username[0]) {
        curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME, cfg.username);
        curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD, cfg.password);
    }
}

static bool http_get(const std::string& url,
                     const ProxyConfig& proxy,
                     std::string& body,
                     std::string& status) {
    body.clear();
    CURL* curl = curl_easy_init();
    if (!curl) {
        status = "curl 初始化失败";
        return false;
    }

    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 6L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NXAnime/0.3 NintendoSwitch");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    apply_proxy(curl, proxy);

    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        status = "网络请求失败: ";
        status += errbuf[0] ? errbuf : curl_easy_strerror(rc);
        return false;
    }
    if (code < 200 || code >= 400) {
        char tmp[64];
        std::snprintf(tmp, sizeof(tmp), "HTTP %ld", code);
        status = tmp;
        return false;
    }
    if (body.empty()) {
        status = "服务器返回空内容";
        return false;
    }
    return true;
}

static std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

static std::string html_decode(std::string s) {
    s = replace_all(s, "&nbsp;", " ");
    s = replace_all(s, "&amp;", "&");
    s = replace_all(s, "&quot;", "\"");
    s = replace_all(s, "&#39;", "'");
    s = replace_all(s, "&#039;", "'");
    s = replace_all(s, "&lt;", "<");
    s = replace_all(s, "&gt;", ">");
    return s;
}

static std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    return s;
}

static std::string strip_tags(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_tag = false;
    bool last_space = false;
    for (char ch : s) {
        if (ch == '<') { in_tag = true; continue; }
        if (ch == '>') {
            in_tag = false;
            if (!out.empty() && !last_space) { out.push_back(' '); last_space = true; }
            continue;
        }
        if (in_tag) continue;
        unsigned char u = (unsigned char)ch;
        if (u < 0x80 && std::isspace(u)) {
            if (!out.empty() && !last_space) out.push_back(' ');
            last_space = true;
        } else {
            out.push_back(ch);
            last_space = false;
        }
    }
    return trim(html_decode(out));
}

static std::string attr_value(const std::string& tag, const char* name) {
    std::string key = std::string(name) + "=";
    size_t p = tag.find(key);
    if (p == std::string::npos) return {};
    p += key.size();
    while (p < tag.size() && std::isspace((unsigned char)tag[p])) ++p;
    if (p >= tag.size()) return {};
    char q = tag[p];
    if (q == '\'' || q == '\"') {
        size_t e = tag.find(q, p + 1);
        if (e == std::string::npos) return {};
        return html_decode(tag.substr(p + 1, e - p - 1));
    }
    size_t e = p;
    while (e < tag.size() && !std::isspace((unsigned char)tag[e]) && tag[e] != '>') ++e;
    return html_decode(tag.substr(p, e - p));
}

static std::string absolute_url(const std::string& u) {
    if (u.empty()) return {};
    if (u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0) return u;
    if (u.rfind("//", 0) == 0) return std::string("https:") + u;
    if (u[0] == '/') return std::string(BASE_URL) + u;
    return std::string(BASE_URL) + "/" + u;
}

static bool looks_like_anime_path(const std::string& href) {
    if (href.size() < 5) return false;
    if (href.find("/GV") == std::string::npos) return false;
    if (href.find("/playGV") != std::string::npos) return false;
    return true;
}

static std::string id_from_url(const std::string& url) {
    size_t p = url.find("GV");
    if (p == std::string::npos) return url;
    size_t e = p + 2;
    while (e < url.size() && std::isdigit((unsigned char)url[e])) ++e;
    return url.substr(p, e - p);
}

static std::string title_from_anchor(const std::string& anchor, const std::string& fallback) {
    size_t open_end = anchor.find('>');
    std::string open = open_end == std::string::npos ? anchor : anchor.substr(0, open_end + 1);
    std::string title = attr_value(open, "title");

    if (title.empty()) {
        size_t ip = anchor.find("<img");
        if (ip != std::string::npos) {
            size_t ie = anchor.find('>', ip);
            if (ie != std::string::npos) {
                std::string img = anchor.substr(ip, ie - ip + 1);
                title = attr_value(img, "alt");
                if (title == "海报图" || title == "imageUrl") title.clear();
            }
        }
    }

    if (title.empty()) {
        std::string visible = strip_tags(anchor);
        size_t play = visible.find("立即播放");
        if (play != std::string::npos) visible.erase(play);
        title = trim(visible);
    }

    if (title.empty() || title.size() > 180) title = fallback;
    return title;
}

static std::string cover_from_anchor(const std::string& anchor) {
    size_t pos = 0;
    while (true) {
        size_t ip = anchor.find("<img", pos);
        if (ip == std::string::npos) break;
        size_t ie = anchor.find('>', ip);
        if (ie == std::string::npos) break;
        std::string tag = anchor.substr(ip, ie - ip + 1);
        const char* attrs[] = {"data-src", "data-original", "data-lazy-src", "src"};
        for (const char* a : attrs) {
            std::string u = attr_value(tag, a);
            if (u.empty()) continue;
            if (u.rfind("data:", 0) == 0) continue;
            std::string low = u;
            std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c){ return (char)std::tolower(c); });
            if (low.find("logo") != std::string::npos || low.find("avatar") != std::string::npos || low.find("loading") != std::string::npos) continue;
            return absolute_url(u);
        }
        pos = ie + 1;
    }
    return {};
}

static void parse_home(const std::string& html, std::vector<AnimeItem>& out) {
    out.clear();
    std::set<std::string> seen;
    size_t pos = 0;

    while (true) {
        size_t h = html.find("href=", pos);
        if (h == std::string::npos) break;
        size_t v = h + 5;
        while (v < html.size() && std::isspace((unsigned char)html[v])) ++v;
        if (v >= html.size()) break;
        char q = html[v];
        size_t b = v;
        size_t e = std::string::npos;
        if (q == '\'' || q == '\"') {
            b = v + 1;
            e = html.find(q, b);
        } else {
            e = b;
            while (e < html.size() && !std::isspace((unsigned char)html[e]) && html[e] != '>') ++e;
        }
        if (e == std::string::npos) break;
        std::string href = html.substr(b, e - b);
        pos = e + 1;
        if (!looks_like_anime_path(href)) continue;

        std::string url = absolute_url(href);
        std::string id = id_from_url(url);
        if (!seen.insert(id).second) continue;

        size_t astart = html.rfind("<a", h);
        size_t aend = html.find("</a>", e);
        std::string anchor;
        if (astart != std::string::npos && aend != std::string::npos && aend > astart && aend - astart < 10000)
            anchor = html.substr(astart, aend + 4 - astart);

        AnimeItem item;
        item.id = id;
        item.url = url;
        item.title = title_from_anchor(anchor, id);
        item.extra = "GiriGiri";
        item.cover_url = cover_from_anchor(anchor);
        out.push_back(item);
        if (out.size() >= 60) break;
    }
}

static std::string extract_heading(const std::string& html) {
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

static std::string extract_meta_value(const std::string& html,
                                      const char* wanted_name,
                                      const char* wanted_prop) {
    size_t pos = 0;
    while (true) {
        size_t m = html.find("<meta", pos);
        if (m == std::string::npos) break;
        size_t e = html.find('>', m);
        if (e == std::string::npos) break;
        std::string tag = html.substr(m, e - m + 1);
        std::string name = attr_value(tag, "name");
        std::string prop = attr_value(tag, "property");
        if ((wanted_name && name == wanted_name) || (wanted_prop && prop == wanted_prop)) {
            std::string c = attr_value(tag, "content");
            if (!c.empty()) return trim(c);
        }
        pos = e + 1;
    }
    return {};
}

static std::string extract_meta_description(const std::string& html) {
    std::string s = extract_meta_value(html, "description", "og:description");
    return s;
}

static std::string extract_cover(const std::string& html) {
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

static std::string extract_status(const std::string& html) {
    size_t p = html.find("更新至");
    if (p == std::string::npos) return {};
    size_t e = html.find('<', p);
    if (e == std::string::npos || e - p > 160) e = std::min(html.size(), p + 160);
    return trim(strip_tags(html.substr(p, e - p)));
}

static void parse_episodes(const std::string& html, std::vector<EpisodeItem>& out) {
    out.clear();
    std::set<std::string> seen;
    size_t pos = 0;
    while (true) {
        size_t h = html.find("href=", pos);
        if (h == std::string::npos) break;
        size_t v = h + 5;
        while (v < html.size() && std::isspace((unsigned char)html[v])) ++v;
        if (v >= html.size()) break;
        char q = html[v];
        size_t b = v;
        size_t e = std::string::npos;
        if (q == '\'' || q == '\"') {
            b = v + 1;
            e = html.find(q, b);
        } else {
            e = b;
            while (e < html.size() && !std::isspace((unsigned char)html[e]) && html[e] != '>') ++e;
        }
        if (e == std::string::npos) break;
        std::string href = html.substr(b, e - b);
        pos = e + 1;
        if (href.find("/playGV") == std::string::npos) continue;

        std::string url = absolute_url(href);
        if (!seen.insert(url).second) continue;
        size_t open_end = html.find('>', h);
        size_t close = html.find("</a>", open_end);
        std::string label;
        if (open_end != std::string::npos && close != std::string::npos && close - open_end < 256)
            label = strip_tags(html.substr(open_end + 1, close - open_end - 1));
        if (label.empty()) {
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%02zu", out.size() + 1);
            label = tmp;
        }
        out.push_back({label, url});
        if (out.size() >= 100) break;
    }
}

bool provider_init(std::string& status) {
    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) {
        status = "curl_global_init 失败";
        return false;
    }
    status = "网络模块已就绪";
    return true;
}

void provider_exit() {
    curl_global_cleanup();
}

bool provider_fetch_home(const ProxyConfig& proxy,
                         std::vector<AnimeItem>& out,
                         std::string& status) {
    std::string html;
    if (!http_get(std::string(BASE_URL) + "/", proxy, html, status)) return false;
    parse_home(html, out);
    if (out.empty()) {
        status = "已连接网站，但未解析到番剧目录";
        return false;
    }
    char tmp[96];
    std::snprintf(tmp, sizeof(tmp), "已载入 %zu 部番剧", out.size());
    status = tmp;
    return true;
}

bool provider_fetch_detail(const ProxyConfig& proxy,
                           const AnimeItem& item,
                           AnimeDetail& out,
                           std::string& status) {
    std::string html;
    if (!http_get(item.url, proxy, html, status)) return false;
    out = AnimeDetail{};
    out.id = item.id;
    out.url = item.url;
    out.title = extract_heading(html);
    if (out.title.empty()) out.title = item.title;
    out.status = extract_status(html);
    out.description = extract_meta_description(html);
    out.cover_url = extract_cover(html);
    if (out.cover_url.empty()) out.cover_url = item.cover_url;
    parse_episodes(html, out.episodes);

    char tmp[128];
    std::snprintf(tmp, sizeof(tmp), "详情载入完成，共 %zu 个公开集数入口", out.episodes.size());
    status = tmp;
    return true;
}

bool provider_test_source(const ProxyConfig& proxy, std::string& status) {
    std::string html;
    if (!http_get(std::string(BASE_URL) + "/", proxy, html, status)) return false;
    status = "GiriGiri 连接正常";
    return true;
}
