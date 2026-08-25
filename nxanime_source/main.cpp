#include <switch.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "provider.hpp"
#include "proxy.h"

#define FB_W 1280
#define FB_H 720

static u32 g_stride_px = 0;
static FT_Library g_ft = nullptr;
static std::vector<FT_Face> g_faces;
static PadState g_pad;

enum Screen {
    SCREEN_HOME = 0,
    SCREEN_DETAIL,
    SCREEN_EPISODE,
    SCREEN_NETWORK,
    SCREEN_ABOUT,
};

struct AppState {
    Screen screen = SCREEN_HOME;
    ProxyConfig proxy{};
    std::vector<AnimeItem> catalog;
    std::vector<size_t> view;
    AnimeDetail detail;
    int selected = 0;
    int episode_selected = 0;
    int network_row = 0;
    std::string search;
    std::string status;
    bool net_ok = false;
};

static inline u32 rgba(u8 r, u8 g, u8 b, u8 a = 255) {
    return (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24);
}

static void fill_rect(u32* fb, int x, int y, int w, int h, u32 c) {
    if (!fb) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB_W) w = FB_W - x;
    if (y + h > FB_H) h = FB_H - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; ++yy) {
        u32* row = fb + yy * g_stride_px + x;
        for (int xx = 0; xx < w; ++xx) row[xx] = c;
    }
}

static void stroke_rect(u32* fb, int x, int y, int w, int h, int t, u32 c) {
    fill_rect(fb, x, y, w, t, c);
    fill_rect(fb, x, y + h - t, w, t, c);
    fill_rect(fb, x, y, t, h, c);
    fill_rect(fb, x + w - t, y, t, h, c);
}

static void blend_pixel(u32* fb, int x, int y, u32 fg, u8 a) {
    if (!fb || x < 0 || y < 0 || x >= FB_W || y >= FB_H || a == 0) return;
    u32* p = fb + y * g_stride_px + x;
    u32 bg = *p;
    u8 fr = fg & 0xff, fg_ = (fg >> 8) & 0xff, fb_ = (fg >> 16) & 0xff;
    u8 br = bg & 0xff, bg_ = (bg >> 8) & 0xff, bb = (bg >> 16) & 0xff;
    u8 r = (u8)((fr * a + br * (255 - a)) / 255);
    u8 g = (u8)((fg_ * a + bg_ * (255 - a)) / 255);
    u8 b = (u8)((fb_ * a + bb * (255 - a)) / 255);
    *p = rgba(r, g, b, 255);
}

static FT_Face choose_face(uint32_t cp) {
    for (FT_Face face : g_faces) {
        if (face && FT_Get_Char_Index(face, cp) != 0) return face;
    }
    return g_faces.empty() ? nullptr : g_faces.front();
}

static void set_font_size(int px) {
    for (FT_Face face : g_faces) {
        if (face) FT_Set_Pixel_Sizes(face, 0, px);
    }
}

static int glyph_advance(FT_Face face, uint32_t cp) {
    if (!face) return 0;
    FT_UInt gi = FT_Get_Char_Index(face, cp);
    if (FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT)) return 0;
    int adv = (int)(face->glyph->advance.x >> 6);
    if (adv <= 0) adv = (int)(face->size->metrics.max_advance >> 6);
    return adv;
}

static void draw_glyph(u32* fb, FT_Face face, uint32_t cp, int x, int baseline, u32 color) {
    if (!face) return;
    FT_UInt gi = FT_Get_Char_Index(face, cp);
    if (FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT)) return;
    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) return;
    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap* bm = &slot->bitmap;
    if (bm->pixel_mode != FT_PIXEL_MODE_GRAY) return;
    int ox = x + slot->bitmap_left;
    int oy = baseline - slot->bitmap_top;
    for (unsigned int yy = 0; yy < bm->rows; ++yy) {
        const unsigned char* row = bm->buffer + yy * bm->pitch;
        for (unsigned int xx = 0; xx < bm->width; ++xx) {
            blend_pixel(fb, ox + (int)xx, oy + (int)yy, color, row[xx]);
        }
    }
}

static int draw_text(u32* fb, int x, int baseline, int px, const std::string& s,
                     u32 color, int max_width = 0, int max_lines = 1) {
    if (g_faces.empty()) return baseline;
    set_font_size(px);
    int start_x = x;
    int line_h = px + 8;
    int lines = 1;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = 0;
        ssize_t units = decode_utf8(&cp, (const uint8_t*)&s[i]);
        if (units <= 0) break;
        i += (size_t)units;
        if (cp == '\r') continue;
        if (cp == '\n') {
            if (lines >= max_lines) break;
            ++lines;
            x = start_x;
            baseline += line_h;
            continue;
        }
        FT_Face face = choose_face(cp);
        int adv = glyph_advance(face, cp);
        if (max_width > 0 && x + adv > start_x + max_width) {
            if (lines >= max_lines) break;
            ++lines;
            x = start_x;
            baseline += line_h;
        }
        draw_glyph(fb, face, cp, x, baseline, color);
        x += adv;
    }
    return baseline;
}

static bool init_fonts(std::string& err) {
    if (FT_Init_FreeType(&g_ft)) {
        err = "FreeType 初始化失败";
        return false;
    }
    const PlSharedFontType types[] = {
        PlSharedFontType_Standard,
        PlSharedFontType_ChineseSimplified,
        PlSharedFontType_ExtChineseSimplified,
        PlSharedFontType_ChineseTraditional,
    };
    for (PlSharedFontType t : types) {
        PlFontData data{};
        if (R_FAILED(plGetSharedFontByType(&data, t))) continue;
        FT_Face face = nullptr;
        if (FT_New_Memory_Face(g_ft, (const FT_Byte*)data.address, (FT_Long)data.size, 0, &face) == 0)
            g_faces.push_back(face);
    }
    if (g_faces.empty()) {
        err = "无法读取 Switch 系统字体";
        FT_Done_FreeType(g_ft);
        g_ft = nullptr;
        return false;
    }
    return true;
}

static void shutdown_fonts() {
    for (FT_Face f : g_faces) if (f) FT_Done_Face(f);
    g_faces.clear();
    if (g_ft) FT_Done_FreeType(g_ft);
    g_ft = nullptr;
}

void userAppInit(void) {
    Result rc = plInitialize(PlServiceType_User);
    if (R_FAILED(rc)) diagAbortWithResult(rc);
}

void userAppExit(void) {
    plExit();
}

static bool keyboard_text(const char* header, const char* guide, const std::string& initial, std::string& out) {
    SwkbdConfig kbd{};
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, header);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetInitialText(&kbd, initial.c_str());
    char buf[768] = {0};
    Result rc = swkbdShow(&kbd, buf, sizeof(buf));
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return false;
    out = buf;
    return true;
}

static std::string lower_ascii(std::string s) {
    for (char& c : s) if ((unsigned char)c < 0x80) c = (char)std::tolower((unsigned char)c);
    return s;
}

static void rebuild_view(AppState& st) {
    st.view.clear();
    std::string key = lower_ascii(st.search);
    for (size_t i = 0; i < st.catalog.size(); ++i) {
        if (key.empty()) st.view.push_back(i);
        else {
            std::string t = lower_ascii(st.catalog[i].title);
            if (t.find(key) != std::string::npos || st.catalog[i].id.find(st.search) != std::string::npos)
                st.view.push_back(i);
        }
    }
    if (st.selected >= (int)st.view.size()) st.selected = std::max(0, (int)st.view.size() - 1);
}

static void draw_header(u32* fb, const char* right_text = nullptr) {
    u32 bg = rgba(10, 11, 16);
    u32 panel = rgba(18, 20, 29);
    u32 accent = rgba(255, 62, 126);
    u32 white = rgba(245, 246, 250);
    u32 muted = rgba(155, 160, 176);
    fill_rect(fb, 0, 0, FB_W, FB_H, bg);
    fill_rect(fb, 0, 0, FB_W, 88, panel);
    fill_rect(fb, 0, 86, FB_W, 2, accent);
    draw_text(fb, 40, 58, 38, "NXAnime", white, 300, 1);
    draw_text(fb, 230, 56, 20, "GiriGiri Provider", accent, 300, 1);
    if (right_text) draw_text(fb, 930, 55, 18, right_text, muted, 300, 1);
}

static void draw_footer(u32* fb, const std::string& status, const char* hints) {
    u32 panel = rgba(18, 20, 29);
    u32 white = rgba(235, 237, 244);
    u32 muted = rgba(144, 149, 165);
    fill_rect(fb, 0, 640, FB_W, 80, panel);
    draw_text(fb, 38, 672, 18, status, muted, 760, 1);
    draw_text(fb, 825, 672, 17, hints ? hints : "", white, 420, 1);
}

static void draw_home(u32* fb, const AppState& st) {
    draw_header(fb, st.net_ok ? "ONLINE" : "OFFLINE");
    u32 white = rgba(245, 246, 250);
    u32 muted = rgba(150, 155, 171);
    u32 accent = rgba(255, 62, 126);
    u32 card = rgba(24, 27, 37);
    u32 selected = rgba(37, 40, 53);

    draw_text(fb, 40, 128, 25, "动漫首页", white, 260, 1);
    std::string sub = st.search.empty() ? "实时读取公共番剧目录" : ("搜索: " + st.search);
    draw_text(fb, 40, 158, 17, sub, muted, 600, 1);
    draw_text(fb, 1020, 154, 16, "X 搜索  ZR 网络", muted, 220, 1);

    if (st.view.empty()) {
        fill_rect(fb, 40, 205, 1200, 360, card);
        draw_text(fb, 80, 285, 30, "没有可显示的番剧", white, 700, 1);
        draw_text(fb, 80, 330, 20, "请按 Y 刷新，或按 ZR 检查网络/代理设置。", muted, 950, 2);
        draw_footer(fb, st.status, "Y 刷新   + 退出");
        return;
    }

    int rows = 7;
    int start = 0;
    if (st.selected >= rows) start = st.selected - rows + 1;
    for (int r = 0; r < rows; ++r) {
        int vi = start + r;
        if (vi >= (int)st.view.size()) break;
        const AnimeItem& item = st.catalog[st.view[vi]];
        int y = 190 + r * 62;
        bool is_sel = vi == st.selected;
        fill_rect(fb, 40, y, 1200, 54, is_sel ? selected : card);
        if (is_sel) fill_rect(fb, 40, y, 6, 54, accent);
        draw_text(fb, 66, y + 36, 22, item.title, white, 900, 1);
        draw_text(fb, 1020, y + 34, 16, item.id, is_sel ? accent : muted, 180, 1);
    }

    char counter[64];
    std::snprintf(counter, sizeof(counter), "%d / %zu", st.selected + 1, st.view.size());
    draw_text(fb, 1080, 610, 16, counter, muted, 160, 1);
    draw_footer(fb, st.status, "A 详情   Y 刷新   + 退出");
}

static void draw_detail(u32* fb, const AppState& st) {
    draw_header(fb, "DETAIL");
    u32 white = rgba(245, 246, 250);
    u32 muted = rgba(150, 155, 171);
    u32 accent = rgba(255, 62, 126);
    u32 card = rgba(24, 27, 37);
    u32 selected = rgba(37, 40, 53);

    draw_text(fb, 40, 135, 30, st.detail.title, white, 1180, 1);
    std::string stat = st.detail.status.empty() ? st.detail.id : (st.detail.status + "  ·  " + st.detail.id);
    draw_text(fb, 40, 170, 18, stat, accent, 1000, 1);

    fill_rect(fb, 40, 195, 1200, 128, card);
    std::string desc = st.detail.description.empty() ? "暂无简介。" : st.detail.description;
    draw_text(fb, 62, 230, 18, desc, muted, 1150, 4);

    draw_text(fb, 40, 360, 22, "公开集数入口", white, 300, 1);
    if (st.detail.episodes.empty()) {
        draw_text(fb, 40, 410, 20, "页面没有解析到集数入口。", muted, 700, 1);
    } else {
        const int cols = 8;
        const int cell_w = 136;
        const int cell_h = 58;
        int start = (st.episode_selected / (cols * 3)) * (cols * 3);
        for (int i = 0; i < cols * 3; ++i) {
            int ei = start + i;
            if (ei >= (int)st.detail.episodes.size()) break;
            int col = i % cols;
            int row = i / cols;
            int x = 40 + col * 149;
            int y = 392 + row * 70;
            bool sel = ei == st.episode_selected;
            fill_rect(fb, x, y, cell_w, cell_h, sel ? selected : card);
            stroke_rect(fb, x, y, cell_w, cell_h, sel ? 3 : 1, sel ? accent : rgba(62, 66, 82));
            draw_text(fb, x + 14, y + 37, 18, st.detail.episodes[ei].label, sel ? white : muted, cell_w - 28, 1);
        }
    }

    draw_footer(fb, st.status, "A 选择集数   B 返回");
}

static void draw_episode(u32* fb, const AppState& st) {
    draw_header(fb, "EPISODE");
    u32 white = rgba(245, 246, 250);
    u32 muted = rgba(150, 155, 171);
    u32 accent = rgba(255, 62, 126);
    u32 card = rgba(24, 27, 37);

    std::string label = "集数";
    std::string url;
    if (!st.detail.episodes.empty() && st.episode_selected < (int)st.detail.episodes.size()) {
        label = st.detail.episodes[st.episode_selected].label;
        url = st.detail.episodes[st.episode_selected].url;
    }
    draw_text(fb, 40, 145, 32, st.detail.title + " · " + label, white, 1180, 1);
    fill_rect(fb, 40, 190, 1200, 330, card);
    draw_text(fb, 70, 245, 23, "播放器接口已预留", accent, 600, 1);
    draw_text(fb, 70, 295, 19,
              "这一版只读取网站公开目录、详情和集数页面，不会破解隐藏视频地址、DRM 或访问控制。",
              muted, 1110, 3);
    draw_text(fb, 70, 390, 18, "集数页面:", white, 180, 1);
    draw_text(fb, 70, 425, 16, url, muted, 1110, 2);
    draw_text(fb, 70, 490, 17, "后续播放器模块可接入公开、合法、非 DRM 的 MP4/HLS 或本地媒体源。", muted, 1110, 2);
    draw_footer(fb, st.status, "B 返回");
}

static const char* network_label(int row) {
    switch (row) {
        case 0: return "模式";
        case 1: return "服务器";
        case 2: return "端口";
        case 3: return "用户名";
        case 4: return "密码";
        case 5: return "测试 GiriGiri";
        case 6: return "保存并返回";
        default: return "";
    }
}

static void draw_network(u32* fb, const AppState& st) {
    draw_header(fb, "NETWORK");
    u32 white = rgba(245, 246, 250);
    u32 muted = rgba(150, 155, 171);
    u32 accent = rgba(255, 62, 126);
    u32 card = rgba(24, 27, 37);
    u32 selected = rgba(37, 40, 53);

    draw_text(fb, 40, 137, 27, "应用内网络 / 代理", white, 500, 1);
    draw_text(fb, 40, 171, 17, "DIRECT / HTTP CONNECT / SOCKS5", muted, 700, 1);

    for (int row = 0; row < 7; ++row) {
        int y = 205 + row * 58;
        bool sel = row == st.network_row;
        fill_rect(fb, 40, y, 1200, 50, sel ? selected : card);
        if (sel) fill_rect(fb, 40, y, 6, 50, accent);
        draw_text(fb, 68, y + 33, 18, network_label(row), sel ? white : muted, 280, 1);
        std::string value;
        char tmp[64];
        if (row == 0) value = proxy_mode_name(st.proxy.mode);
        else if (row == 1) value = st.proxy.host[0] ? st.proxy.host : "未设置";
        else if (row == 2) { std::snprintf(tmp, sizeof(tmp), "%d", st.proxy.port); value = tmp; }
        else if (row == 3) value = st.proxy.username[0] ? st.proxy.username : "可选";
        else if (row == 4) value = st.proxy.password[0] ? "********" : "可选";
        else if (row == 5) value = "A 开始测试";
        else value = "A 保存";
        draw_text(fb, 390, y + 33, 18, value, sel ? accent : white, 780, 1);
    }
    draw_footer(fb, st.status, "A 编辑/执行   B 返回");
}

static void draw_about(u32* fb, const AppState& st) {
    draw_header(fb, "ABOUT");
    u32 white = rgba(245, 246, 250);
    u32 muted = rgba(150, 155, 171);
    u32 accent = rgba(255, 62, 126);
    u32 card = rgba(24, 27, 37);
    fill_rect(fb, 40, 145, 1200, 380, card);
    draw_text(fb, 70, 205, 31, "NXAnime v0.1", white, 500, 1);
    draw_text(fb, 70, 255, 20, "作者: LINKO", accent, 400, 1);
    draw_text(fb, 70, 310, 18,
              "目标：在 Nintendo Switch 上提供原生动漫浏览与播放器框架。当前 Provider 仅读取 GiriGiri 公共页面中的目录、详情和集数入口。",
              muted, 1110, 4);
    draw_text(fb, 70, 435, 18,
              "不会绕过付费、登录、DRM 或其他访问控制。后续可接入本地媒体以及公开合法的非 DRM 视频源。",
              muted, 1110, 3);
    draw_footer(fb, st.status, "B 返回");
}

static void render(Framebuffer& fb, const AppState& st) {
    u32 stride = 0;
    u32* buf = (u32*)framebufferBegin(&fb, &stride);
    g_stride_px = stride / sizeof(u32);
    if (st.screen == SCREEN_HOME) draw_home(buf, st);
    else if (st.screen == SCREEN_DETAIL) draw_detail(buf, st);
    else if (st.screen == SCREEN_EPISODE) draw_episode(buf, st);
    else if (st.screen == SCREEN_NETWORK) draw_network(buf, st);
    else draw_about(buf, st);
    framebufferEnd(&fb);
}

static void render_loading(Framebuffer& fb, const std::string& title, const std::string& sub) {
    u32 stride = 0;
    u32* buf = (u32*)framebufferBegin(&fb, &stride);
    g_stride_px = stride / sizeof(u32);
    draw_header(buf, "LOADING");
    u32 white = rgba(245, 246, 250);
    u32 muted = rgba(150, 155, 171);
    u32 accent = rgba(255, 62, 126);
    fill_rect(buf, 40, 190, 1200, 260, rgba(24, 27, 37));
    fill_rect(buf, 70, 245, 12, 110, accent);
    draw_text(buf, 110, 285, 30, title, white, 1000, 2);
    draw_text(buf, 110, 340, 19, sub, muted, 1000, 3);
    framebufferEnd(&fb);
}

static void refresh_catalog(AppState& st, Framebuffer& fb) {
    if (!st.net_ok) {
        st.status = "网络服务未初始化，请确认 Wi-Fi";
        return;
    }
    render_loading(fb, "正在连接 GiriGiri…", "读取网站公开番剧目录，请稍候。代理设置会自动应用。" );
    std::vector<AnimeItem> items;
    std::string msg;
    if (provider_fetch_home(st.proxy, items, msg)) {
        st.catalog.swap(items);
        st.status = msg;
        rebuild_view(st);
    } else {
        st.status = msg;
        st.catalog.clear();
        rebuild_view(st);
    }
}

static void open_detail(AppState& st, Framebuffer& fb) {
    if (st.view.empty() || st.selected < 0 || st.selected >= (int)st.view.size()) return;
    const AnimeItem item = st.catalog[st.view[st.selected]];
    render_loading(fb, "正在读取番剧详情…", item.title);
    AnimeDetail d;
    std::string msg;
    if (provider_fetch_detail(st.proxy, item, d, msg)) {
        st.detail = std::move(d);
        st.episode_selected = 0;
        st.screen = SCREEN_DETAIL;
    }
    st.status = msg;
}

static void do_search(AppState& st) {
    std::string q = st.search;
    if (!keyboard_text("搜索番剧", "搜索当前已载入的 GiriGiri 公共目录；留空显示全部", q, q)) return;
    st.search = q;
    st.selected = 0;
    rebuild_view(st);
    st.status = st.search.empty() ? "已清除搜索" : ("搜索: " + st.search);
}

static void edit_proxy_field(AppState& st, int row) {
    std::string value;
    if (row == 1) value = st.proxy.host;
    else if (row == 2) { char tmp[32]; std::snprintf(tmp, sizeof(tmp), "%d", st.proxy.port); value = tmp; }
    else if (row == 3) value = st.proxy.username;
    else if (row == 4) value = st.proxy.password;
    else return;

    const char* header = row == 1 ? "代理服务器" : row == 2 ? "代理端口" : row == 3 ? "代理用户名" : "代理密码";
    const char* guide = row == 1 ? "例如 192.168.1.20 或 proxy.example.com" : row == 2 ? "1 - 65535" : "可留空";
    if (!keyboard_text(header, guide, value, value)) return;
    if (row == 1) std::snprintf(st.proxy.host, sizeof(st.proxy.host), "%s", value.c_str());
    else if (row == 2) {
        int p = std::atoi(value.c_str());
        if (p > 0 && p <= 65535) st.proxy.port = p;
    } else if (row == 3) std::snprintf(st.proxy.username, sizeof(st.proxy.username), "%s", value.c_str());
    else std::snprintf(st.proxy.password, sizeof(st.proxy.password), "%s", value.c_str());
}

static void handle_network_a(AppState& st, Framebuffer& fb) {
    int row = st.network_row;
    if (row == 0) {
        st.proxy.mode = (ProxyMode)(((int)st.proxy.mode + 1) % 3);
        st.status = std::string("网络模式: ") + proxy_mode_name(st.proxy.mode);
    } else if (row >= 1 && row <= 4) {
        edit_proxy_field(st, row);
    } else if (row == 5) {
        render_loading(fb, "正在测试连接…", "目标: https://ani.girigirilove.com/");
        std::string msg;
        bool ok = provider_test_source(st.proxy, msg);
        st.status = ok ? ("测试成功: " + msg) : ("测试失败: " + msg);
    } else if (row == 6) {
        if (proxy_config_save(&st.proxy)) st.status = "网络设置已保存";
        else st.status = "保存失败";
        st.screen = SCREEN_HOME;
        refresh_catalog(st, fb);
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);

    std::string font_err;
    if (!init_fonts(font_err)) return 1;

    Framebuffer fb;
    Result frc = framebufferCreate(&fb, nwindowGetDefault(), FB_W, FB_H, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(frc)) {
        shutdown_fonts();
        return (int)frc;
    }
    framebufferMakeLinear(&fb);

    AppState st;
    proxy_config_load(&st.proxy);
    Result sr = socketInitializeDefault();
    st.net_ok = R_SUCCEEDED(sr);

    std::string pstatus;
    bool provider_ok = st.net_ok && provider_init(pstatus);
    if (!provider_ok && st.net_ok) st.status = pstatus;
    else st.status = st.net_ok ? "网络模块已就绪" : "Wi-Fi / socket 初始化失败";

    if (provider_ok) refresh_catalog(st, fb);

    bool running = true;
    while (running && appletMainLoop()) {
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad);
        if (down & HidNpadButton_Plus) break;

        if (down & HidNpadButton_ZR) {
            st.screen = SCREEN_NETWORK;
            st.network_row = 0;
        }

        if (st.screen == SCREEN_HOME) {
            if (!st.view.empty()) {
                if (down & HidNpadButton_Up) st.selected = (st.selected - 1 + (int)st.view.size()) % (int)st.view.size();
                if (down & HidNpadButton_Down) st.selected = (st.selected + 1) % (int)st.view.size();
                if (down & HidNpadButton_A) open_detail(st, fb);
            }
            if (down & HidNpadButton_Y) refresh_catalog(st, fb);
            if (down & HidNpadButton_X) do_search(st);
            if (down & HidNpadButton_Minus) st.screen = SCREEN_ABOUT;
        } else if (st.screen == SCREEN_DETAIL) {
            int n = (int)st.detail.episodes.size();
            if (n > 0) {
                if (down & HidNpadButton_Left) st.episode_selected = (st.episode_selected - 1 + n) % n;
                if (down & HidNpadButton_Right) st.episode_selected = (st.episode_selected + 1) % n;
                if (down & HidNpadButton_Up) st.episode_selected = std::max(0, st.episode_selected - 8);
                if (down & HidNpadButton_Down) st.episode_selected = std::min(n - 1, st.episode_selected + 8);
                if (down & HidNpadButton_A) st.screen = SCREEN_EPISODE;
            }
            if (down & HidNpadButton_B) st.screen = SCREEN_HOME;
        } else if (st.screen == SCREEN_EPISODE) {
            if (down & HidNpadButton_B) st.screen = SCREEN_DETAIL;
        } else if (st.screen == SCREEN_NETWORK) {
            if (down & HidNpadButton_Up) st.network_row = (st.network_row + 6) % 7;
            if (down & HidNpadButton_Down) st.network_row = (st.network_row + 1) % 7;
            if (down & HidNpadButton_A) handle_network_a(st, fb);
            if (down & HidNpadButton_B) {
                proxy_config_save(&st.proxy);
                st.screen = SCREEN_HOME;
            }
        } else if (st.screen == SCREEN_ABOUT) {
            if (down & HidNpadButton_B) st.screen = SCREEN_HOME;
        }

        render(fb, st);
        svcSleepThread(10'000'000);
    }

    if (provider_ok) provider_exit();
    if (st.net_ok) socketExit();
    framebufferClose(&fb);
    shutdown_fonts();
    return 0;
}
