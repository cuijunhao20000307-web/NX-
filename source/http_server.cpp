#include "http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <vector>

static std::string html_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o += c; break;
        }
    }
    return o;
}

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string url_decode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int a = hexv(s[i+1]), b = hexv(s[i+2]);
            if (a >= 0 && b >= 0) { o.push_back((char)((a<<4)|b)); i += 2; continue; }
        }
        o.push_back(s[i] == '+' ? ' ' : s[i]);
    }
    return o;
}

static std::map<std::string,std::string> parse_query(const std::string& q) {
    std::map<std::string,std::string> m;
    size_t p = 0;
    while (p <= q.size()) {
        size_t amp = q.find('&', p);
        if (amp == std::string::npos) amp = q.size();
        std::string item = q.substr(p, amp-p);
        size_t eq = item.find('=');
        if (eq == std::string::npos) m[url_decode(item)] = "";
        else m[url_decode(item.substr(0,eq))] = url_decode(item.substr(eq+1));
        if (amp == q.size()) break;
        p = amp + 1;
    }
    return m;
}

static bool send_all(int fd, const void* data, size_t n) {
    const char* p = (const char*)data;
    while (n) {
        ssize_t w = send(fd, p, n, 0);
        if (w <= 0) return false;
        p += w; n -= (size_t)w;
    }
    return true;
}

static void send_response(int fd, int code, const char* type, const std::string& body) {
    std::ostringstream h;
    h << "HTTP/1.1 " << code << (code==200 ? " OK" : " Error") << "\r\n";
    h << "Content-Type: " << type << "; charset=utf-8\r\n";
    h << "Content-Length: " << body.size() << "\r\n";
    h << "Connection: close\r\n\r\n";
    auto hs = h.str();
    send_all(fd, hs.data(), hs.size());
    send_all(fd, body.data(), body.size());
}

HttpServer::HttpServer(SelectedState& state) : state_(state) {}
HttpServer::~HttpServer() { stop(); }

bool HttpServer::start(uint16_t port) {
    if (state_.server_running.load()) return true;
    port_ = port;
    stop_ = false;

    in_addr a{};
    a.s_addr = gethostid();
    const char* s = inet_ntoa(a);
    ip_ = s ? s : "0.0.0.0";
    if (ip_ == "0.0.0.0" || ip_ == "127.0.0.1") return false;

    failed_ = false;
    worker_ = std::thread([this]{ run(); });
    for (int i = 0; i < 150; ++i) {
        if (state_.server_running.load()) return true;
        if (failed_.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    stop();
    return false;
}

void HttpServer::stop() {
    stop_ = true;
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (worker_.joinable()) worker_.join();
    state_.server_running = false;
}

std::string HttpServer::url() const {
    return "http://" + ip_ + ":" + std::to_string(port_) + "/";
}

void HttpServer::run() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) { failed_ = true; return; }
    int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = inet_addr(ip_.c_str());
    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0 || listen(listen_fd_, 4) < 0) {
        close(listen_fd_); listen_fd_ = -1; failed_ = true; return;
    }
    state_.server_running = true;

    while (!stop_) {
        sockaddr_in cli{}; socklen_t len = sizeof(cli);
        int fd = accept(listen_fd_, (sockaddr*)&cli, &len);
        if (fd < 0) {
            if (stop_) break;
            continue;
        }
        handle_client(fd);
        close(fd);
    }
    state_.server_running = false;
}

void HttpServer::handle_client(int fd) {
    std::string req;
    char buf[4096];
    size_t header_end = std::string::npos;
    while ((header_end = req.find("\r\n\r\n")) == std::string::npos && req.size() < 65536) {
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if (r <= 0) return;
        req.append(buf, (size_t)r);
    }
    if (header_end == std::string::npos) return;

    std::istringstream hs(req.substr(0, header_end));
    std::string method, target, version;
    hs >> method >> target >> version;
    std::string line;
    size_t content_len = 0;
    std::getline(hs, line);
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto p = line.find(':');
        if (p == std::string::npos) continue;
        std::string k = line.substr(0,p), v = line.substr(p+1);
        while (!v.empty() && v.front() == ' ') v.erase(v.begin());
        for (auto& c : k) c = (char)tolower((unsigned char)c);
        if (k == "content-length") content_len = (size_t)strtoull(v.c_str(), nullptr, 10);
    }

    if (content_len > 8 * 1024 * 1024) {
        send_response(fd, 413, "text/plain", "Image too large (max 8 MiB).\n");
        return;
    }

    std::vector<unsigned char> body;
    size_t have = req.size() - (header_end + 4);
    body.insert(body.end(), req.begin() + header_end + 4, req.end());
    while (have < content_len) {
        ssize_t r = recv(fd, buf, std::min(sizeof(buf), content_len-have), 0);
        if (r <= 0) break;
        body.insert(body.end(), buf, buf + r);
        have += (size_t)r;
    }
    if (body.size() > content_len) body.resize(content_len);

    if (method == "GET") {
        GameEntry g;
        {
            std::lock_guard<std::mutex> lk(state_.mutex);
            g = state_.game;
        }
        std::string page = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>NXTitleStudio</title><style>
body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;background:#111;color:#eee;margin:0;padding:22px}main{max-width:620px;margin:auto}.card{background:#1b1b1b;border-radius:18px;padding:20px;margin:12px 0}h1{font-size:25px}label{display:block;margin-top:14px;font-size:13px;color:#aaa}input{width:100%;box-sizing:border-box;padding:13px;border-radius:11px;border:1px solid #444;background:#0c0c0c;color:#fff;font-size:16px}button{width:100%;padding:14px;margin-top:18px;border:0;border-radius:12px;background:#fff;color:#111;font-weight:700;font-size:16px}.danger{background:#ff5f57}.small{color:#999;font-size:12px;word-break:break-all}#msg{white-space:pre-wrap}</style></head><body><main><h1>NXTitleStudio</h1><div class="card"><b>)HTML";
        page += html_escape(g.name);
        page += "</b><div class=\"small\">" + title_id_hex(g.title_id) + R"HTML(</div>
<label>Game name</label><input id="name" maxlength="512" value=")HTML" + html_escape(g.name) + R"HTML(">
<label>Publisher / author</label><input id="author" maxlength="256" value=")HTML" + html_escape(g.author) + R"HTML(">
<label>Display version</label><input id="ver" maxlength="16" value=")HTML" + html_escape(g.version) + R"HTML(">
<label>New icon (PNG/JPG, optional)</label><input id="img" type="file" accept="image/*">
<button onclick="apply()">Apply override</button><button class="danger" onclick="restore()">Restore original</button><p id="msg"></p></div></main><script>
const q=v=>encodeURIComponent(v);
async function apply(){let f=document.getElementById('img').files[0];let body=f?await f.arrayBuffer():new ArrayBuffer(0);msg.textContent='Applying...';let u='/apply?name='+q(name.value)+'&author='+q(author.value)+'&version='+q(ver.value);let r=await fetch(u,{method:'POST',body});msg.textContent=await r.text();}
async function restore(){if(!confirm('Restore original metadata/icon override?'))return;let r=await fetch('/restore',{method:'POST'});msg.textContent=await r.text();}
</script></body></html>)HTML";
        send_response(fd, 200, "text/html", page);
        return;
    }

    std::string path = target, query;
    auto qm = target.find('?');
    if (qm != std::string::npos) { path = target.substr(0,qm); query = target.substr(qm+1); }

    if (method == "POST" && path == "/apply") {
        auto q = parse_query(query);
        GameEntry g;
        { std::lock_guard<std::mutex> lk(state_.mutex); g = state_.game; }
        std::string err;
        bool ok = apply_override(g, q["name"], q["author"], q["version"], body, err);
        {
            std::lock_guard<std::mutex> lk(state_.mutex);
            state_.status = ok ? "Applied. Reboot Switch to refresh HOME menu cache." : ("Failed: " + err);
        }
        if (ok) state_.changed = true;
        send_response(fd, ok ? 200 : 500, "text/plain", ok ? "Applied. Reboot the Switch to refresh HOME menu.\n" : ("Failed: " + err + "\n"));
        return;
    }

    if (method == "POST" && path == "/restore") {
        GameEntry g;
        { std::lock_guard<std::mutex> lk(state_.mutex); g = state_.game; }
        std::string err;
        bool ok = restore_override(g.title_id, err);
        {
            std::lock_guard<std::mutex> lk(state_.mutex);
            state_.status = ok ? "Override removed. Reboot Switch." : ("Failed: " + err);
        }
        if (ok) state_.changed = true;
        send_response(fd, ok ? 200 : 500, "text/plain", ok ? "Restored override files. Reboot the Switch.\n" : ("Failed: " + err + "\n"));
        return;
    }

    send_response(fd, 404, "text/plain", "Not found\n");
}
