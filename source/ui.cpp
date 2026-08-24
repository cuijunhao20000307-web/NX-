#include "ui.hpp"

#include <switch.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "qrcodegen.hpp"

namespace {

constexpr int W = 1280;
constexpr int H = 720;

struct HosFont {
    PlFontData data{};
    stbtt_fontinfo info{};
    bool ready = false;
};

Framebuffer g_fb{};
bool g_fb_ready = false;
bool g_pl_ready = false;
HosFont g_standard;
HosFont g_cn;
HosFont g_cn_ext;

u32* g_pixels = nullptr;
u32 g_stride_px = 0;

constexpr u32 C_BG        = RGBA8(10, 13, 18, 255);
constexpr u32 C_PANEL     = RGBA8(18, 23, 31, 255);
constexpr u32 C_PANEL_2   = RGBA8(24, 30, 39, 255);
constexpr u32 C_ROW       = RGBA8(21, 27, 36, 255);
constexpr u32 C_ROW_SEL   = RGBA8(29, 44, 52, 255);
constexpr u32 C_ACCENT    = RGBA8(45, 226, 214, 255);
constexpr u32 C_TEXT      = RGBA8(242, 245, 248, 255);
constexpr u32 C_MUTED     = RGBA8(151, 163, 177, 255);
constexpr u32 C_LINE      = RGBA8(45, 53, 65, 255);
constexpr u32 C_SUCCESS   = RGBA8(70, 220, 120, 255);
constexpr u32 C_WARN      = RGBA8(255, 190, 80, 255);
constexpr u32 C_WHITE     = RGBA8(255, 255, 255, 255);
constexpr u32 C_BLACK     = RGBA8(0, 0, 0, 255);

inline void put_pixel(int x, int y, u32 color) {
    if (!g_pixels || x < 0 || y < 0 || x >= W || y >= H) return;
    g_pixels[y * g_stride_px + x] = color;
}

void fill_rect(int x, int y, int w, int h, u32 color) {
    if (!g_pixels || w <= 0 || h <= 0) return;
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(W, x + w);
    int y1 = std::min(H, y + h);
    for (int yy = y0; yy < y1; ++yy) {
        u32* row = g_pixels + yy * g_stride_px + x0;
        std::fill(row, row + (x1 - x0), color);
    }
}

void line_h(int x, int y, int w, u32 color) {
    fill_rect(x, y, w, 1, color);
}

void fill_circle(int cx, int cy, int r, u32 color) {
    for (int y = -r; y <= r; ++y) {
        int xx = (int)std::sqrt((double)std::max(0, r * r - y * y));
        fill_rect(cx - xx, cy + y, xx * 2 + 1, 1, color);
    }
}

void blend_pixel(int x, int y, u32 src, unsigned char coverage) {
    if (!g_pixels || x < 0 || y < 0 || x >= W || y >= H || coverage == 0) return;
    u32& dst = g_pixels[y * g_stride_px + x];

    int a = coverage;
    int sr = src & 0xff;
    int sg = (src >> 8) & 0xff;
    int sb = (src >> 16) & 0xff;
    int dr = dst & 0xff;
    int dg = (dst >> 8) & 0xff;
    int db = (dst >> 16) & 0xff;

    int rr = (sr * a + dr * (255 - a)) / 255;
    int rg = (sg * a + dg * (255 - a)) / 255;
    int rb = (sb * a + db * (255 - a)) / 255;
    dst = RGBA8(rr, rg, rb, 255);
}

uint32_t next_utf8(const char*& p) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(p);
    if (!*s) return 0;
    if (s[0] < 0x80) {
        ++p;
        return s[0];
    }
    if ((s[0] & 0xE0) == 0xC0 && s[1]) {
        uint32_t cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        p += 2;
        return cp;
    }
    if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
        uint32_t cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        p += 3;
        return cp;
    }
    if ((s[0] & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
        uint32_t cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                      ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        p += 4;
        return cp;
    }
    ++p;
    return '?';
}

HosFont* font_for(uint32_t cp) {
    HosFont* fonts[] = {&g_standard, &g_cn, &g_cn_ext};
    for (HosFont* f : fonts) {
        if (f->ready && stbtt_FindGlyphIndex(&f->info, (int)cp) != 0) return f;
    }
    return g_standard.ready ? &g_standard : (g_cn.ready ? &g_cn : &g_cn_ext);
}

int glyph_advance(HosFont* f, uint32_t cp, float px) {
    if (!f || !f->ready) return (int)(px * 0.6f);
    float scale = stbtt_ScaleForPixelHeight(&f->info, px);
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&f->info, (int)cp, &adv, &lsb);
    return std::max(1, (int)std::round(adv * scale));
}

int text_width(const std::string& text, float px) {
    int x = 0;
    const char* p = text.c_str();
    while (*p) {
        uint32_t cp = next_utf8(p);
        if (cp == '\n') break;
        x += glyph_advance(font_for(cp), cp, px);
    }
    return x;
}

void draw_text(int x, int y, const std::string& text, float px, u32 color, int max_width = 0) {
    const int start_x = x;
    const int baseline = y + (int)std::round(px * 0.82f);
    const char* p = text.c_str();

    while (*p) {
        uint32_t cp = next_utf8(p);
        if (cp == '\n') break;
        HosFont* f = font_for(cp);
        if (!f || !f->ready) continue;
        int advance = glyph_advance(f, cp, px);
        if (max_width > 0 && x + advance > start_x + max_width) break;

        float scale = stbtt_ScaleForPixelHeight(&f->info, px);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&f->info, (int)cp, scale, scale, &x0, &y0, &x1, &y1);
        int gw = x1 - x0;
        int gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            std::vector<unsigned char> bitmap((size_t)gw * gh);
            stbtt_MakeCodepointBitmap(&f->info, bitmap.data(), gw, gh, gw, scale, scale, (int)cp);
            for (int yy = 0; yy < gh; ++yy) {
                for (int xx = 0; xx < gw; ++xx) {
                    unsigned char a = bitmap[(size_t)yy * gw + xx];
                    if (a) blend_pixel(x + x0 + xx, baseline + y0 + yy, color, a);
                }
            }
        }
        x += advance;
    }
}

bool init_font(HosFont& out, PlSharedFontType type) {
    if (R_FAILED(plGetSharedFontByType(&out.data, type)) || !out.data.address || !out.data.size) return false;
    int off = stbtt_GetFontOffsetForIndex(reinterpret_cast<const unsigned char*>(out.data.address), 0);
    if (off < 0) return false;
    out.ready = stbtt_InitFont(&out.info, reinterpret_cast<const unsigned char*>(out.data.address), off) != 0;
    return out.ready;
}

void begin_frame() {
    u32 stride = 0;
    g_pixels = reinterpret_cast<u32*>(framebufferBegin(&g_fb, &stride));
    g_stride_px = stride / sizeof(u32);
    if (g_pixels) fill_rect(0, 0, W, H, C_BG);
}

void end_frame() {
    framebufferEnd(&g_fb);
    g_pixels = nullptr;
    g_stride_px = 0;
}

void draw_header(const char* page_title, const char* page_subtitle) {
    fill_rect(0, 0, W, 86, RGBA8(13, 17, 23, 255));
    fill_rect(42, 28, 8, 32, C_ACCENT);
    draw_text(66, 20, page_title, 31.0f, C_TEXT);
    draw_text(66, 54, page_subtitle, 16.0f, C_MUTED);

    fill_rect(1052, 24, 180, 36, C_PANEL_2);
    fill_circle(1074, 42, 5, C_SUCCESS);
    draw_text(1090, 31, "HOS 系统字体", 17.0f, C_TEXT);
}

void draw_footer(const std::string& left, const std::string& right = {}) {
    line_h(42, 663, 1196, C_LINE);
    draw_text(48, 679, left, 18.0f, C_TEXT);
    if (!right.empty()) {
        int tw = text_width(right, 17.0f);
        draw_text(W - 48 - tw, 680, right, 17.0f, C_MUTED);
    }
}

void draw_label_value(int x, int y, const char* label, const std::string& value, u32 value_color = C_TEXT) {
    draw_text(x, y, label, 17.0f, C_MUTED);
    draw_text(x + 116, y - 1, value, 18.0f, value_color, 250);
}

} // namespace

bool ui_init(std::string& error) {
    Result rc = plInitialize(PlServiceType_User);
    if (R_FAILED(rc)) {
        char b[96];
        std::snprintf(b, sizeof(b), "pl:u init failed: 0x%08X", rc);
        error = b;
        return false;
    }
    g_pl_ready = true;

    init_font(g_standard, PlSharedFontType_Standard);
    init_font(g_cn, PlSharedFontType_ChineseSimplified);
    init_font(g_cn_ext, PlSharedFontType_ExtChineseSimplified);
    if (!g_standard.ready && !g_cn.ready && !g_cn_ext.ready) {
        error = "HOS shared font unavailable";
        ui_exit();
        return false;
    }

    rc = framebufferCreate(&g_fb, nwindowGetDefault(), W, H, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) {
        char b[96];
        std::snprintf(b, sizeof(b), "framebufferCreate failed: 0x%08X", rc);
        error = b;
        ui_exit();
        return false;
    }
    g_fb_ready = true;

    rc = framebufferMakeLinear(&g_fb);
    if (R_FAILED(rc)) {
        char b[96];
        std::snprintf(b, sizeof(b), "framebufferMakeLinear failed: 0x%08X", rc);
        error = b;
        ui_exit();
        return false;
    }
    return true;
}

void ui_exit() {
    if (g_fb_ready) {
        framebufferClose(&g_fb);
        g_fb_ready = false;
    }
    if (g_pl_ready) {
        plExit();
        g_pl_ready = false;
    }
}

void ui_draw_game_list(const std::vector<GameEntry>& games, int selected, const std::string& status) {
    if (!g_fb_ready) return;
    begin_frame();
    if (!g_pixels) { end_frame(); return; }

    draw_header("NXTitleStudio", "游戏名称 / 图标安全覆盖工具 · Atmosphère + sys-ticon");

    fill_rect(42, 106, 760, 532, C_PANEL);
    fill_rect(822, 106, 416, 532, C_PANEL);

    draw_text(64, 124, "游戏列表", 21.0f, C_TEXT);
    draw_text(676, 126, std::to_string(games.size()) + " 个", 16.0f, C_MUTED);
    line_h(62, 158, 720, C_LINE);

    if (games.empty()) {
        draw_text(82, 210, "没有找到已安装游戏", 25.0f, C_TEXT);
        draw_text(82, 250, "请确认从完整 HBMenu 环境启动。", 18.0f, C_MUTED);
    } else {
        constexpr int rows = 8;
        int start = (selected / rows) * rows;
        int end = std::min((int)games.size(), start + rows);
        int y = 172;
        for (int i = start; i < end; ++i, y += 56) {
            const bool sel = i == selected;
            if (sel) {
                fill_rect(58, y - 4, 728, 50, C_ROW_SEL);
                fill_rect(58, y - 4, 5, 50, C_ACCENT);
            } else {
                fill_rect(58, y - 4, 728, 50, C_ROW);
            }

            char idx[8];
            std::snprintf(idx, sizeof(idx), "%02d", i + 1);
            draw_text(76, y + 5, idx, 16.0f, sel ? C_ACCENT : C_MUTED);
            draw_text(116, y, games[i].name, 20.0f, C_TEXT, 510);
            draw_text(116, y + 25, title_id_hex(games[i].title_id), 13.0f, C_MUTED);
            if (sel) draw_text(756, y + 4, ">", 22.0f, C_ACCENT);
        }

        const GameEntry& game = games[selected];
        draw_text(848, 126, "当前选择", 21.0f, C_TEXT);
        line_h(846, 158, 368, C_LINE);

        fill_rect(848, 182, 70, 70, C_PANEL_2);
        fill_rect(848, 182, 6, 70, C_ACCENT);
        draw_text(869, 197, "NX", 25.0f, C_ACCENT);
        draw_text(934, 181, game.name, 25.0f, C_TEXT, 268);
        draw_text(934, 218, "已安装应用", 16.0f, C_MUTED);

        draw_label_value(850, 286, "Title ID", title_id_hex(game.title_id));
        draw_label_value(850, 326, "版本", game.version.empty() ? "-" : game.version);
        draw_label_value(850, 366, "厂商", game.author.empty() ? "-" : game.author);
        draw_label_value(850, 406, "覆盖方式", "sys-ticon");

        fill_rect(848, 466, 364, 96, C_PANEL_2);
        draw_text(866, 480, "快捷操作", 17.0f, C_MUTED);
        draw_text(866, 512, "A  手机编辑", 20.0f, C_TEXT);
        draw_text(1042, 512, "X  恢复", 20.0f, C_TEXT);

        if (!status.empty()) {
            fill_rect(848, 578, 364, 42, C_PANEL_2);
            fill_circle(866, 599, 5, C_WARN);
            draw_text(882, 586, status, 16.0f, C_TEXT, 310);
        }
    }

    draw_footer("↑↓ 选择     A 手机编辑     X 恢复覆盖     + 退出", "NXTitleStudio 0.2.0 UI");
    end_frame();
}

void ui_draw_phone_editor(const GameEntry& game, const std::string& url, const std::string& status) {
    if (!g_fb_ready) return;
    begin_frame();
    if (!g_pixels) { end_frame(); return; }

    draw_header("手机编辑", "在同一 Wi‑Fi 下扫码，用手机修改名称、厂商、版本与图标");

    fill_rect(42, 106, 520, 532, C_PANEL);
    fill_rect(582, 106, 656, 532, C_PANEL);

    draw_text(64, 126, "扫描二维码", 21.0f, C_TEXT);
    draw_text(604, 126, "编辑当前游戏", 21.0f, C_TEXT);
    line_h(62, 158, 480, C_LINE);
    line_h(602, 158, 612, C_LINE);

    using qrcodegen::QrCode;
    const QrCode qr = QrCode::encodeText(url.c_str(), QrCode::Ecc::MEDIUM);
    int qn = qr.getSize();
    const int quiet = 4;
    const int total = qn + quiet * 2;
    int module = std::max(1, 390 / total);
    int qr_px = module * total;
    int qx = 42 + (520 - qr_px) / 2;
    int qy = 190;
    fill_rect(qx, qy, qr_px, qr_px, C_WHITE);
    for (int yy = 0; yy < qn; ++yy) {
        for (int xx = 0; xx < qn; ++xx) {
            if (qr.getModule(xx, yy)) {
                fill_rect(qx + (xx + quiet) * module,
                          qy + (yy + quiet) * module,
                          module, module, C_BLACK);
            }
        }
    }

    draw_text(64, 590, "手机与 Switch 必须连接同一个 Wi‑Fi", 16.0f, C_MUTED, 460);

    draw_text(606, 186, game.name, 28.0f, C_TEXT, 590);
    draw_text(606, 224, title_id_hex(game.title_id), 15.0f, C_MUTED);

    fill_rect(606, 276, 586, 154, C_PANEL_2);
    draw_text(628, 294, "操作步骤", 18.0f, C_ACCENT);
    draw_text(628, 330, "1. 用手机相机扫描左侧二维码", 19.0f, C_TEXT);
    draw_text(628, 364, "2. 打开局域网页并修改内容", 19.0f, C_TEXT);
    draw_text(628, 398, "3. 点击应用，完成后重启 Switch", 19.0f, C_TEXT);

    draw_text(606, 458, "连接地址", 17.0f, C_MUTED);
    fill_rect(606, 488, 586, 45, C_BG);
    draw_text(622, 499, url, 16.0f, C_TEXT, 548);

    if (!status.empty()) {
        fill_rect(606, 558, 586, 58, C_PANEL_2);
        fill_circle(626, 587, 6, C_SUCCESS);
        draw_text(646, 572, status, 17.0f, C_TEXT, 520);
    } else {
        draw_text(606, 570, "等待手机连接…", 18.0f, C_MUTED);
    }

    draw_footer("B 返回游戏列表     + 退出", "HOS 字体渲染");
    end_frame();
}
