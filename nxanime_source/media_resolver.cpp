#include "media_resolver.hpp"

#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cstdio>

static size_t media_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    if (!ptr || !userdata) return 0;
    auto* out = static_cast<std::string*>(userdata);
    size_t n = size * nmemb;
    out->append(static_cast<const char*>(ptr), n);
    return n;
}

static void media_apply_proxy(CURL* c, const ProxyConfig& cfg) {
    if (!c) return;
    if (cfg.mode == PROXY_MODE_DIRECT) {
        curl_easy_setopt(c, CURLOPT_PROXY, "");
        return;
    }
    if (!cfg.host[0] || cfg.port <= 0) return;
    char addr[384];
    std::snprintf(addr, sizeof(addr), "%s:%d", cfg.host, cfg.port);
    curl_easy_setopt(c, CURLOPT_PROXY, addr);
    curl_easy_setopt(c, CURLOPT_PROXYTYPE,
                     cfg.mode == PROXY_MODE_HTTP ? CURLPROXY_HTTP : CURLPROXY_SOCKS5_HOSTNAME);
    if (cfg.username[0]) {
        curl_easy_setopt(c, CURLOPT_PROXYUSERNAME, cfg.username);
        curl_easy_setopt(c, CURLOPT_PROXYPASSWORD, cfg.password);
    }
}

static bool media_http_get(const std::string& url, const ProxyConfig& proxy,
                           std::string& body, std::string& status) {
    body.clear();
    CURL* c = curl_easy_init();
    if (!c) { status = "播放器网络初始化失败"; return false; }
    char err[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 6L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 18L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Nintendo Switch; NXAnime) AppleWebKit/605.1.15 Version/17.0 Safari/605.1.15");
    curl_easy_setopt(c, CURLOPT_REFERER, "https://ani.girigirilove.com/");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, media_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    media_apply_proxy(c, proxy);
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) {
        status = std::string("播放页请求失败：") + (err[0] ? err : curl_easy_strerror(rc));
        return false;
    }
    if (code < 200 || code >= 400) {
        char tmp[64]; std::snprintf(tmp, sizeof(tmp), "播放页 HTTP %ld", code); status = tmp; return false;
    }
    return !body.empty();
}

static std::string clean_url(std::string s) {
    auto rep = [&](const std::string& a, const std::string& b) {
        size_t p = 0;
        while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, a.size(), b); p += b.size(); }
    };
    rep("\\/", "/"); rep("&amp;", "&"); rep("\\u0026", "&");
    while (!s.empty() && (s.back()=='\\' || s.back()=='\"' || s.back()=='\'' || std::isspace((unsigned char)s.back()))) s.pop_back();
    return s;
}

static bool is_direct_media(const std::string& u) {
    if (u.rfind("http://",0) != 0 && u.rfind("https://",0) != 0) return false;
    std::string low = u;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c){return (char)std::tolower(c);});
    return low.find(".m3u8") != std::string::npos || low.find(".mp4") != std::string::npos;
}

static std::string literal_media_url(const std::string& html) {
    const char* ext[] = {".m3u8", ".mp4"};
    for (const char* e : ext) {
        size_t from = 0;
        while (true) {
            size_t p = html.find(e, from);
            if (p == std::string::npos) break;
            size_t b = html.rfind("http", p);
            if (b != std::string::npos && p - b < 1600) {
                size_t end = p + std::strlen(e);
                while (end < html.size()) {
                    char c = html[end];
                    if (c=='\"' || c=='\'' || c=='<' || c=='>' || std::isspace((unsigned char)c)) break;
                    ++end;
                }
                std::string u = clean_url(html.substr(b, end-b));
                if (is_direct_media(u)) return u;
            }
            from = p + 1;
        }
    }
    return {};
}

static std::string attr_direct_media(const std::string& html) {
    size_t pos = 0;
    while (true) {
        size_t p = html.find("src=", pos);
        if (p == std::string::npos) break;
        p += 4;
        while (p < html.size() && std::isspace((unsigned char)html[p])) ++p;
        char q = p < html.size() ? html[p] : 0;
        if (q=='\"' || q=='\'') ++p; else q=0;
        size_t e = p;
        while (e < html.size()) {
            if ((q && html[e]==q) || (!q && (std::isspace((unsigned char)html[e]) || html[e]=='>'))) break;
            ++e;
        }
        std::string u = clean_url(html.substr(p,e-p));
        if (is_direct_media(u)) return u;
        pos = e + 1;
    }
    return {};
}

bool media_resolve_public(const ProxyConfig& proxy,
                          const EpisodeItem& episode,
                          MediaSource& out,
                          std::string& status) {
    out = MediaSource{};
    if (episode.url.empty()) { status = "该集没有播放页地址"; return false; }
    std::string html;
    if (!media_http_get(episode.url, proxy, html, status)) return false;

    std::string u = attr_direct_media(html);
    if (u.empty()) u = literal_media_url(html);
    if (!u.empty()) {
        out.url = u;
        out.referer = episode.url;
        std::string low=u; std::transform(low.begin(),low.end(),low.begin(),[](unsigned char c){return (char)std::tolower(c);});
        out.hls = low.find(".m3u8") != std::string::npos;
        status = out.hls ? "已找到公开 HLS 播放源" : "已找到公开 MP4 播放源";
        return true;
    }

    if (html.find("\"encrypt\":2") != std::string::npos || html.find("\"encrypt\" : 2") != std::string::npos) {
        status = "该集使用站点加密地址（encrypt:2），暂不支持原生播放";
        return false;
    }
    if (html.find("\"encrypt\":1") != std::string::npos || html.find("\"encrypt\" : 1") != std::string::npos) {
        status = "该集使用站点编码播放地址，暂不支持原生播放";
        return false;
    }
    status = "播放页没有直接公开 MP4/HLS 地址";
    return false;
}
