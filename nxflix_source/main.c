#include <stdio.h>
#include <string.h>
#include <switch.h>

#define SCREEN_W 1280
#define SCREEN_H 720
#define URL_HOME  "https://www.netflix.com/"
#define URL_LOGIN "https://www.netflix.com/login"

static Result g_last_rc = 0;
static int g_notice_frames = 0;
static bool g_last_blocked = false;

static inline u32 col(u8 r, u8 g, u8 b) { return RGBA8(r, g, b, 255); }

static void fill_rect(u32 *buf, u32 pitch, int x, int y, int w, int h, u32 c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; yy++) {
        u32 *row = &buf[yy * pitch + x];
        for (int xx = 0; xx < w; xx++) row[xx] = c;
    }
}

static void border_rect(u32 *buf, u32 pitch, int x, int y, int w, int h, int t, u32 c) {
    fill_rect(buf, pitch, x, y, w, t, c);
    fill_rect(buf, pitch, x, y+h-t, w, t, c);
    fill_rect(buf, pitch, x, y, t, h, c);
    fill_rect(buf, pitch, x+w-t, y, t, h, c);
}

static void fill_circle(u32 *buf, u32 pitch, int cx, int cy, int r, u32 c) {
    for (int y = -r; y <= r; y++) {
        int yy = cy + y;
        int rr = r*r - y*y;
        int xlim = 0;
        while ((xlim+1)*(xlim+1) <= rr) xlim++;
        fill_rect(buf, pitch, cx-xlim, yy, xlim*2+1, 1, c);
    }
}

static void glyph5(char ch, u8 out[5]) {
    memset(out, 0, 5);
    switch (ch) {
        case 'A': {u8 v[5]={0x7C,0x12,0x11,0x12,0x7C}; memcpy(out,v,5);} break;
        case 'B': {u8 v[5]={0x7F,0x49,0x49,0x49,0x36}; memcpy(out,v,5);} break;
        case 'C': {u8 v[5]={0x3E,0x41,0x41,0x41,0x22}; memcpy(out,v,5);} break;
        case 'D': {u8 v[5]={0x7F,0x41,0x41,0x22,0x1C}; memcpy(out,v,5);} break;
        case 'E': {u8 v[5]={0x7F,0x49,0x49,0x49,0x41}; memcpy(out,v,5);} break;
        case 'F': {u8 v[5]={0x7F,0x09,0x09,0x09,0x01}; memcpy(out,v,5);} break;
        case 'G': {u8 v[5]={0x3E,0x41,0x49,0x49,0x7A}; memcpy(out,v,5);} break;
        case 'H': {u8 v[5]={0x7F,0x08,0x08,0x08,0x7F}; memcpy(out,v,5);} break;
        case 'I': {u8 v[5]={0x00,0x41,0x7F,0x41,0x00}; memcpy(out,v,5);} break;
        case 'J': {u8 v[5]={0x20,0x40,0x41,0x3F,0x01}; memcpy(out,v,5);} break;
        case 'K': {u8 v[5]={0x7F,0x08,0x14,0x22,0x41}; memcpy(out,v,5);} break;
        case 'L': {u8 v[5]={0x7F,0x40,0x40,0x40,0x40}; memcpy(out,v,5);} break;
        case 'M': {u8 v[5]={0x7F,0x02,0x0C,0x02,0x7F}; memcpy(out,v,5);} break;
        case 'N': {u8 v[5]={0x7F,0x04,0x08,0x10,0x7F}; memcpy(out,v,5);} break;
        case 'O': {u8 v[5]={0x3E,0x41,0x41,0x41,0x3E}; memcpy(out,v,5);} break;
        case 'P': {u8 v[5]={0x7F,0x09,0x09,0x09,0x06}; memcpy(out,v,5);} break;
        case 'Q': {u8 v[5]={0x3E,0x41,0x51,0x21,0x5E}; memcpy(out,v,5);} break;
        case 'R': {u8 v[5]={0x7F,0x09,0x19,0x29,0x46}; memcpy(out,v,5);} break;
        case 'S': {u8 v[5]={0x46,0x49,0x49,0x49,0x31}; memcpy(out,v,5);} break;
        case 'T': {u8 v[5]={0x01,0x01,0x7F,0x01,0x01}; memcpy(out,v,5);} break;
        case 'U': {u8 v[5]={0x3F,0x40,0x40,0x40,0x3F}; memcpy(out,v,5);} break;
        case 'V': {u8 v[5]={0x1F,0x20,0x40,0x20,0x1F}; memcpy(out,v,5);} break;
        case 'W': {u8 v[5]={0x3F,0x40,0x38,0x40,0x3F}; memcpy(out,v,5);} break;
        case 'X': {u8 v[5]={0x63,0x14,0x08,0x14,0x63}; memcpy(out,v,5);} break;
        case 'Y': {u8 v[5]={0x07,0x08,0x70,0x08,0x07}; memcpy(out,v,5);} break;
        case 'Z': {u8 v[5]={0x61,0x51,0x49,0x45,0x43}; memcpy(out,v,5);} break;
        case '0': {u8 v[5]={0x3E,0x51,0x49,0x45,0x3E}; memcpy(out,v,5);} break;
        case '1': {u8 v[5]={0x00,0x42,0x7F,0x40,0x00}; memcpy(out,v,5);} break;
        case '2': {u8 v[5]={0x42,0x61,0x51,0x49,0x46}; memcpy(out,v,5);} break;
        case '3': {u8 v[5]={0x21,0x41,0x45,0x4B,0x31}; memcpy(out,v,5);} break;
        case '4': {u8 v[5]={0x18,0x14,0x12,0x7F,0x10}; memcpy(out,v,5);} break;
        case '5': {u8 v[5]={0x27,0x45,0x45,0x45,0x39}; memcpy(out,v,5);} break;
        case '6': {u8 v[5]={0x3C,0x4A,0x49,0x49,0x30}; memcpy(out,v,5);} break;
        case '7': {u8 v[5]={0x01,0x71,0x09,0x05,0x03}; memcpy(out,v,5);} break;
        case '8': {u8 v[5]={0x36,0x49,0x49,0x49,0x36}; memcpy(out,v,5);} break;
        case '9': {u8 v[5]={0x06,0x49,0x49,0x29,0x1E}; memcpy(out,v,5);} break;
        case '!': {u8 v[5]={0x00,0x00,0x5F,0x00,0x00}; memcpy(out,v,5);} break;
        case '.': {u8 v[5]={0x00,0x60,0x60,0x00,0x00}; memcpy(out,v,5);} break;
        case ':': {u8 v[5]={0x00,0x36,0x36,0x00,0x00}; memcpy(out,v,5);} break;
        case '-': {u8 v[5]={0x08,0x08,0x08,0x08,0x08}; memcpy(out,v,5);} break;
        case '+': {u8 v[5]={0x08,0x08,0x3E,0x08,0x08}; memcpy(out,v,5);} break;
        case '/': {u8 v[5]={0x20,0x10,0x08,0x04,0x02}; memcpy(out,v,5);} break;
        case '(': {u8 v[5]={0x00,0x1C,0x22,0x41,0x00}; memcpy(out,v,5);} break;
        case ')': {u8 v[5]={0x00,0x41,0x22,0x1C,0x00}; memcpy(out,v,5);} break;
        default: break;
    }
}

static int text_width(const char *s, int scale) {
    int n = 0;
    for (; *s; s++) n += 6 * scale;
    return n ? n - scale : 0;
}

static void draw_char(u32 *buf, u32 pitch, int x, int y, char ch, int scale, u32 c) {
    u8 g[5];
    glyph5(ch, g);
    for (int cx = 0; cx < 5; cx++) {
        for (int cy = 0; cy < 7; cy++) {
            if (g[cx] & (1u << cy))
                fill_rect(buf, pitch, x + cx*scale, y + cy*scale, scale, scale, c);
        }
    }
}

static void draw_text(u32 *buf, u32 pitch, int x, int y, const char *s, int scale, u32 c) {
    for (; *s; s++) {
        draw_char(buf, pitch, x, y, *s, scale, c);
        x += 6 * scale;
    }
}

static void draw_button(u32 *buf, u32 pitch, int x, int y, int w, int h,
                        const char *key, const char *title, const char *sub) {
    u32 panel = col(27,28,32);
    u32 edge  = col(72,72,78);
    u32 red   = col(220,30,42);
    u32 white = col(244,244,246);
    u32 gray  = col(170,171,178);
    fill_rect(buf, pitch, x, y, w, h, panel);
    border_rect(buf, pitch, x, y, w, h, 2, edge);
    fill_circle(buf, pitch, x + 58, y + 66, 28, red);
    int kw = text_width(key, 4);
    draw_text(buf, pitch, x + 58 - kw/2, y + 52, key, 4, white);
    draw_text(buf, pitch, x + 106, y + 43, title, 4, white);
    draw_text(buf, pitch, x + 106, y + 95, sub, 2, gray);
    fill_rect(buf, pitch, x + 106, y + h - 28, 90, 4, red);
}

static bool is_application_mode(void) {
    return appletGetAppletType() == AppletType_Application;
}

static Result open_browser(const char *url) {
    WebCommonConfig cfg;
    WebCommonReply reply;
    Result rc = webPageCreate(&cfg, url);
    if (R_FAILED(rc)) return rc;

    rc = webConfigSetWhitelist(&cfg, ".*");
    if (R_FAILED(rc)) return rc;

    webConfigSetPointer(&cfg, true);
    webConfigSetFooter(&cfg, true);
    webConfigSetDisplayUrlKind(&cfg, false);
    if (hosversionAtLeast(3,0,0)) webConfigSetJsExtension(&cfg, true);
    if (hosversionAtLeast(4,0,0)) {
        webConfigSetTouchEnabledOnContents(&cfg, true);
        webConfigSetPageCache(&cfg, true);
        webConfigSetWebAudio(&cfg, true);
    }
    if (hosversionAtLeast(5,0,0)) webConfigSetBootLoadingIcon(&cfg, true);
    if (hosversionAtLeast(6,0,0)) webConfigSetMediaAutoPlay(&cfg, false);
    return webConfigShow(&cfg, &reply);
}

static void draw_ui(u32 *buf, u32 pitch, bool app_mode) {
    u32 bg1   = col(16,17,21);
    u32 red   = col(220,30,42);
    u32 red2  = col(92,18,25);
    u32 white = col(245,245,247);
    u32 gray  = col(164,165,173);
    u32 soft  = col(35,36,42);
    u32 ok    = col(76,190,116);

    for (int y = 0; y < SCREEN_H; y++) {
        u8 v = (u8)(11 + (y * 7 / SCREEN_H));
        fill_rect(buf, pitch, 0, y, SCREEN_W, 1, RGBA8(v, v+1, v+4, 255));
    }
    fill_rect(buf, pitch, 0, 0, SCREEN_W, 8, red);

    for (int yy = 0; yy < 70; yy++) {
        int half = yy / 2;
        fill_rect(buf, pitch, 58, 48 + yy, half + 1, 1, red);
    }

    draw_text(buf, pitch, 132, 52, "NX", 7, white);
    draw_text(buf, pitch, 222, 52, "FLIX", 7, red);
    draw_text(buf, pitch, 410, 61, "WEB LAUNCHER", 5, white);
    draw_text(buf, pitch, 132, 118, "UNOFFICIAL SWITCH WEB LAUNCHER", 2, gray);
    fill_rect(buf, pitch, 132, 146, 230, 4, red);

    fill_rect(buf, pitch, 78, 178, 1124, 146, soft);
    fill_rect(buf, pitch, 78, 178, 220, 146, red2);
    border_rect(buf, pitch, 78, 178, 1124, 146, 3, red);
    fill_circle(buf, pitch, 188, 251, 38, red);
    draw_text(buf, pitch, 175, 226, "!", 6, white);
    draw_text(buf, pitch, 330, 207, "MODE:", 4, white);

    if (app_mode) {
        draw_text(buf, pitch, 478, 207, "APPLICATION MODE - READY", 4, ok);
        draw_text(buf, pitch, 330, 263, "A/X CAN OPEN THE SYSTEM WEB BROWSER.", 2, gray);
    } else {
        draw_text(buf, pitch, 478, 207, "APPLET MODE - LIMITED", 4, red);
        draw_text(buf, pitch, 330, 258, "HOLD R WHILE STARTING A GAME, THEN OPEN HBMENU.", 2, white);
        draw_text(buf, pitch, 330, 286, "BROWSER LAUNCH IS BLOCKED IN THIS MODE.", 2, gray);
    }

    draw_button(buf, pitch, 78, 358, 352, 220, "A", "OPEN NETFLIX", "NETFLIX HOME");
    draw_button(buf, pitch, 464, 358, 352, 220, "X", "LOGIN PAGE", "NETFLIX SIGN IN");
    draw_button(buf, pitch, 850, 358, 352, 220, "+", "EXIT", "RETURN TO HBMENU");

    fill_rect(buf, pitch, 0, 620, SCREEN_W, 100, bg1);
    fill_rect(buf, pitch, 0, 620, SCREEN_W, 2, col(50,51,58));
    draw_text(buf, pitch, 78, 648, "NXFLIX V1.1.0  LINKO", 2, white);
    draw_text(buf, pitch, 78, 681, "WEB ACCESS MAY WORK. VIDEO CAN FAIL IF DRM/EME IS UNAVAILABLE.", 2, gray);

    if (g_notice_frames > 0) {
        fill_rect(buf, pitch, 760, 640, 442, 48, g_last_blocked ? red2 : col(30,55,38));
        border_rect(buf, pitch, 760, 640, 442, 48, 2, g_last_blocked ? red : ok);
        char msg[64];
        if (g_last_blocked) strcpy(msg, "USE APPLICATION MODE");
        else if (R_FAILED(g_last_rc)) snprintf(msg, sizeof(msg), "WEB ERROR 0X%08X", g_last_rc);
        else strcpy(msg, "BROWSER CLOSED");
        draw_text(buf, pitch, 785, 655, msg, 2, white);
    }
}

int main(int argc, char **argv) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    NWindow *win = nwindowGetDefault();
    Framebuffer fb;
    Result rc = framebufferCreate(&fb, win, SCREEN_W, SCREEN_H, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) return (int)rc;
    framebufferMakeLinear(&fb);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        bool app_mode = is_application_mode();
        if (down & HidNpadButton_Plus) break;

        bool open_home = (down & HidNpadButton_A) != 0;
        bool open_login = (down & HidNpadButton_X) != 0;

        u32 stride_bytes = 0;
        u32 *buf = (u32*)framebufferBegin(&fb, &stride_bytes);
        u32 pitch = stride_bytes / sizeof(u32);
        draw_ui(buf, pitch, app_mode);
        framebufferEnd(&fb);

        if (g_notice_frames > 0) g_notice_frames--;

        if (open_home || open_login) {
            if (!app_mode) {
                g_last_blocked = true;
                g_last_rc = 0;
                g_notice_frames = 240;
                continue;
            }
            g_last_blocked = false;
            g_last_rc = open_browser(open_login ? URL_LOGIN : URL_HOME);
            g_notice_frames = 240;
        }
    }

    framebufferClose(&fb);
    return 0;
}
