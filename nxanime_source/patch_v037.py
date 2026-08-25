from pathlib import Path

provider = Path('nxanime_source/provider.cpp')
main = Path('nxanime_source/main.cpp')
p = provider.read_text(encoding='utf-8')
m = main.read_text(encoding='utf-8')

# Keep a shared libcurl cookie/DNS session across requests. GiriGiri search can reject
# a fresh direct search request even though the public homepage itself works.
marker = 'static const char* BASE_URL = "https://ani.girigirilove.com";\n'
insert = marker + 'static CURLSH* g_curl_share = nullptr;\n'
if marker not in p:
    raise SystemExit('BASE_URL marker not found')
p = p.replace(marker, insert, 1)

# Make every request look like one browser session and share cookies obtained from homepage.
old_opts = r'''    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
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
'''
new_opts = r'''    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 6L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 18L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Nintendo Switch; NXAnime) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15");
    curl_easy_setopt(curl, CURLOPT_REFERER, "https://ani.girigirilove.com/");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    if (g_curl_share) curl_easy_setopt(curl, CURLOPT_SHARE, g_curl_share);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    headers = curl_slist_append(headers, "Accept-Language: zh-CN,zh;q=0.9,zh-TW;q=0.8,en;q=0.7");
    headers = curl_slist_append(headers, "Cache-Control: no-cache");
    headers = curl_slist_append(headers, "Pragma: no-cache");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    apply_proxy(curl, proxy);

    CURLcode rc = curl_easy_perform(curl);
'''
if old_opts not in p:
    raise SystemExit('http options block not found')
p = p.replace(old_opts, new_opts, 1)

old_cleanup = r'''    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
'''
new_cleanup = r'''    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);

    if (rc != CURLE_OK) {
'''
if old_cleanup not in p:
    raise SystemExit('http cleanup marker not found')
p = p.replace(old_cleanup, new_cleanup, 1)

# Broaden detail-link detection slightly for search pages while still rejecting play links.
old_looks = r'''static bool looks_like_anime_path(const std::string& href) {
    if (href.size() < 5) return false;
    if (href.find("/GV") == std::string::npos) return false;
    if (href.find("/playGV") != std::string::npos) return false;
    return true;
}
'''
new_looks = r'''static bool looks_like_anime_path(const std::string& href) {
    if (href.size() < 4) return false;
    if (href.find("GV") == std::string::npos) return false;
    if (href.find("playGV") != std::string::npos) return false;
    return true;
}
'''
if old_looks not in p:
    raise SystemExit('looks_like_anime_path block not found')
p = p.replace(old_looks, new_looks, 1)

old_init = r'''bool provider_init(std::string& status) {
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
'''
new_init = r'''bool provider_init(std::string& status) {
    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) {
        status = "curl_global_init 失败";
        return false;
    }
    g_curl_share = curl_share_init();
    if (g_curl_share) {
        curl_share_setopt(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
        curl_share_setopt(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    }
    status = "网络模块已就绪";
    return true;
}

void provider_exit() {
    if (g_curl_share) {
        curl_share_cleanup(g_curl_share);
        g_curl_share = nullptr;
    }
    curl_global_cleanup();
}
'''
if old_init not in p:
    raise SystemExit('provider init block not found')
p = p.replace(old_init, new_init, 1)

# v0.3.2 inserted provider_search before provider_fetch_detail. Replace it with a warm-session version.
start = p.find('bool provider_search(const ProxyConfig& proxy,')
end = p.find('\nbool provider_filter(', start)
if end < 0:
    end = p.find('\nbool provider_fetch_detail(', start)
if start < 0 or end < 0:
    raise SystemExit('provider_search block not found')
new_search = r'''bool provider_search(const ProxyConfig& proxy,
                     const std::string& query,
                     std::vector<AnimeItem>& out,
                     std::string& status) {
    if (query.empty()) return provider_fetch_home(proxy, out, status);

    // Warm the public homepage first so the search request belongs to the same cookie session.
    std::string warm_html, warm_status;
    http_get(std::string(BASE_URL) + "/", proxy, warm_html, warm_status);

    std::string html;
    std::string url = std::string(BASE_URL) + "/search/-------------/?wd=" + url_encode(query);
    if (!http_get(url, proxy, html, status)) return false;

    parse_home(html, out);
    if (out.empty()) {
        if (html.find("非法请求") != std::string::npos)
            status = "搜索被网站拒绝（非法请求）";
        else if (html.find("系统提示") != std::string::npos)
            status = "搜索返回了网站系统提示页";
        else
            status = "网站搜索没有返回番剧";
        return true;
    }

    char tmp[128];
    std::snprintf(tmp, sizeof(tmp), "搜索完成：%zu 个结果", out.size());
    status = tmp;
    return true;
}
'''
p = p[:start] + new_search + p[end:]
p = p.replace('NXAnime/0.3.4 NintendoSwitch', 'NXAnime/0.3.7 NintendoSwitch')
provider.write_text(p, encoding='utf-8')

m = m.replace('page+" · v0.3.6"', 'page+" · v0.3.7"')
m = m.replace('"FUZZY SEARCH 0.3.6"', '"SEARCH SESSION 0.3.7"')
m = m.replace('NXAnime 0.3.6 startup', 'NXAnime 0.3.7 startup')
m = m.replace('部分关键词即可搜索 · 自动模糊匹配 · HOS 中文键盘', '部分关键词搜索 · Cookie 会话修复 · HOS 中文键盘')
main.write_text(m, encoding='utf-8')

print('v0.3.7 persistent cookie/browser search session patch applied')
