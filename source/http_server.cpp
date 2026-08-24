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
        send_response(fd, 413, "text/plain", "图片太大，最大支持 8 MiB。\n");
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
        std::string page = R"HTML(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><meta name="theme-color" content="#101214"><title>NX标题工坊</title><style>
*{box-sizing:border-box}body{font-family:-apple-system,BlinkMacSystemFont,"PingFang SC","Microsoft YaHei",sans-serif;background:#101214;color:#f4f4f5;margin:0;padding:24px 18px 40px}main{max-width:620px;margin:auto}h1{font-size:32px;margin:14px 0 28px;font-weight:800;letter-spacing:-.5px}.card{background:#1b1d1f;border-radius:22px;padding:24px 20px;box-shadow:0 1px 0 rgba(255,255,255,.03) inset}.game-title{font-size:20px;font-weight:750;line-height:1.35}.small{color:#8f9398;font-size:14px;word-break:break-all;margin-top:4px}label{display:block;margin-top:22px;margin-bottom:8px;font-size:15px;color:#a7aaae}input[type=text]{width:100%;padding:15px 14px;border-radius:13px;border:1px solid #484b50;background:#0c0d0e;color:#fff;font-size:17px;outline:none}input[type=text]:focus{border-color:#7f858b}.filebox{margin-top:4px;border:1px solid #484b50;background:#0c0d0e;border-radius:13px;min-height:58px;display:flex;align-items:center;padding:9px 11px;gap:12px}.filebtn{display:inline-flex;align-items:center;justify-content:center;background:#f2f2f3;color:#111;border-radius:10px;padding:9px 14px;font-size:15px;font-weight:700;white-space:nowrap}.filename{font-size:15px;color:#d8d9db;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}button{width:100%;padding:16px;margin-top:20px;border:0;border-radius:14px;background:#f4f4f5;color:#101010;font-weight:800;font-size:18px}.danger{background:#ff5f57;color:#111;margin-top:16px}.hint{font-size:13px;color:#85898e;margin-top:8px}.msg{white-space:pre-wrap;line-height:1.55;margin:18px 2px 0;font-size:15px;color:#dfe1e3;min-height:10px}.section-note{font-size:13px;color:#777c82;margin-top:3px}</style></head><body><main><h1>NX标题工坊</h1><div class="card"><div class="game-title">)HTML";
        page += html_escape(g.name);
        page += "</div><div class=\"small\">标题 ID：" + title_id_hex(g.title_id) + R"HTML(</div>
<label>游戏名称</label><input id="name" type="text" maxlength="512" value=")HTML" + html_escape(g.name) + R"HTML(">
<label>发行商 / 作者</label><input id="author" type="text" maxlength="256" value=")HTML" + html_escape(g.author) + R"HTML(">
<label>显示版本</label><input id="ver" type="text" maxlength="16" value=")HTML" + html_escape(g.version) + R"HTML(">
<label>新图标</label><div class="filebox"><label for="img" class="filebtn" style="margin:0">选择图片</label><span id="filename" class="filename">未选择图片</span></div><input id="img" type="file" accept="image/png,image/jpeg,image/webp" hidden><div class="hint">可选，支持 PNG / JPG / WebP，最大 8 MiB</div>
<button id="applyBtn" onclick="applyOverride()">应用修改</button><button class="danger" onclick="restoreOriginal()">恢复原始设置</button><p id="msg" class="msg"></p></div></main><script>
const el=id=>document.getElementById(id);const q=v=>encodeURIComponent(v);const msg=el('msg');
el('img').addEventListener('change',()=>{const f=el('img').files[0];el('filename').textContent=f?f.name:'未选择图片';});
async function applyOverride(){const btn=el('applyBtn');const f=el('img').files[0];let body=f?await f.arrayBuffer():new ArrayBuffer(0);msg.textContent='正在应用修改…';btn.disabled=true;try{let u='/apply?name='+q(el('name').value)+'&author='+q(el('author').value)+'&version='+q(el('ver').value);let r=await fetch(u,{method:'POST',body});msg.textContent=await r.text();}catch(e){msg.textContent='连接失败，请确认手机与 Switch 仍连接在同一 Wi‑Fi。';}finally{btn.disabled=false;}}
async function restoreOriginal(){if(!confirm('确定恢复此游戏原来的名称、版本和图标覆盖吗？'))return;msg.textContent='正在恢复…';try{let r=await fetch('/restore',{method:'POST'});msg.textContent=await r.text();}catch(e){msg.textContent='连接失败，请确认手机与 Switch 仍连接在同一 Wi‑Fi。';}}
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
            state_.status = ok ? "修改已应用，重启 Switch 后刷新 HOME 菜单" : ("修改失败：" + err);
        }
        if (ok) state_.changed = true;
        send_response(fd, ok ? 200 : 500, "text/plain", ok ? "修改成功。请重启 Switch，让 HOME 菜单刷新名称和图标。\n" : ("修改失败：" + err + "\n"));
        return;
    }

    if (method == "POST" && path == "/restore") {
        GameEntry g;
        { std::lock_guard<std::mutex> lk(state_.mutex); g = state_.game; }
        std::string err;
        bool ok = restore_override(g.title_id, err);
        {
            std::lock_guard<std::mutex> lk(state_.mutex);
            state_.status = ok ? "原始覆盖已恢复，请重启 Switch" : ("恢复失败：" + err);
        }
        if (ok) state_.changed = true;
        send_response(fd, ok ? 200 : 500, "text/plain", ok ? "恢复成功。请重启 Switch。\n" : ("恢复失败：" + err + "\n"));
        return;
    }

    send_response(fd, 404, "text/plain", "页面不存在\n");
}
