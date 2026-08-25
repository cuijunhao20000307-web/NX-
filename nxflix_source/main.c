#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proxy.h"

#define W 1280
#define H 720

typedef enum {
    SCREEN_HOME = 0,
    SCREEN_DETAIL = 1,
    SCREEN_PROXY = 2
} ScreenMode;

static inline u32 rgba(u8 r, u8 g, u8 b, u8 a) {
    return ((u32)r) | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24);
}

static void rect(u32* buf, u32 stride_px, int x, int y, int w, int h, u32 c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; ++yy) {
        u32* row = buf + yy * stride_px + x;
        for (int xx = 0; xx < w; ++xx) row[xx] = c;
    }
}

static void border(u32* buf, u32 stride_px, int x, int y, int w, int h, int t, u32 c) {
    rect(buf, stride_px, x, y, w, t, c);
    rect(buf, stride_px, x, y+h-t, w, t, c);
    rect(buf, stride_px, x, y, t, h, c);
    rect(buf, stride_px, x+w-t, y, t, h, c);
}

static void glyph(char ch, u8 out[7]) {
    memset(out, 0, 7);
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    #define G(a,b,c,d,e,f,g) do {out[0]=a;out[1]=b;out[2]=c;out[3]=d;out[4]=e;out[5]=f;out[6]=g;} while(0)
    switch (ch) {
        case 'A': G(14,17,17,31,17,17,17); break;
        case 'B': G(30,17,17,30,17,17,30); break;
        case 'C': G(14,17,16,16,16,17,14); break;
        case 'D': G(30,17,17,17,17,17,30); break;
        case 'E': G(31,16,16,30,16,16,31); break;
        case 'F': G(31,16,16,30,16,16,16); break;
        case 'G': G(14,17,16,23,17,17,15); break;
        case 'H': G(17,17,17,31,17,17,17); break;
        case 'I': G(31,4,4,4,4,4,31); break;
        case 'J': G(7,2,2,2,18,18,12); break;
        case 'K': G(17,18,20,24,20,18,17); break;
        case 'L': G(16,16,16,16,16,16,31); break;
        case 'M': G(17,27,21,21,17,17,17); break;
        case 'N': G(17,25,21,19,17,17,17); break;
        case 'O': G(14,17,17,17,17,17,14); break;
        case 'P': G(30,17,17,30,16,16,16); break;
        case 'Q': G(14,17,17,17,21,18,13); break;
        case 'R': G(30,17,17,30,20,18,17); break;
        case 'S': G(15,16,16,14,1,1,30); break;
        case 'T': G(31,4,4,4,4,4,4); break;
        case 'U': G(17,17,17,17,17,17,14); break;
        case 'V': G(17,17,17,17,17,10,4); break;
        case 'W': G(17,17,17,21,21,21,10); break;
        case 'X': G(17,17,10,4,10,17,17); break;
        case 'Y': G(17,17,10,4,4,4,4); break;
        case 'Z': G(31,1,2,4,8,16,31); break;
        case '0': G(14,17,19,21,25,17,14); break;
        case '1': G(4,12,4,4,4,4,14); break;
        case '2': G(14,17,1,2,4,8,31); break;
        case '3': G(30,1,1,14,1,1,30); break;
        case '4': G(2,6,10,18,31,2,2); break;
        case '5': G(31,16,16,30,1,1,30); break;
        case '6': G(14,16,16,30,17,17,14); break;
        case '7': G(31,1,2,4,8,8,8); break;
        case '8': G(14,17,17,14,17,17,14); break;
        case '9': G(14,17,17,15,1,1,14); break;
        case ':': G(0,4,4,0,4,4,0); break;
        case '.': G(0,0,0,0,0,6,6); break;
        case '/': G(1,2,2,4,8,8,16); break;
        case '-': G(0,0,0,31,0,0,0); break;
        case '+': G(0,4,4,31,4,4,0); break;
        case ' ': default: break;
    }
    #undef G
}

static void text(u32* buf, u32 stride_px, int x, int y, int scale, const char* s, u32 c) {
    int ox = x;
    for (; *s; ++s) {
        if (*s == '\n') { y += 9 * scale; x = ox; continue; }
        u8 rows[7]; glyph(*s, rows);
        for (int gy=0; gy<7; ++gy) {
            for (int gx=0; gx<5; ++gx) {
                if (rows[gy] & (1 << (4-gx))) {
                    rect(buf, stride_px, x + gx*scale, y + gy*scale, scale, scale, c);
                }
            }
        }
        x += 6 * scale;
    }
}

static void card(u32* buf, u32 stride_px, int x, int y, int w, int h,
                 const char* title, const char* sub, bool selected, int iconKind) {
    u32 panel = rgba(28,29,33,255);
    u32 panel2 = rgba(36,37,42,255);
    u32 white = rgba(239,239,242,255);
    u32 muted = rgba(157,160,168,255);
    u32 red = rgba(229,9,20,255);
    rect(buf, stride_px, x, y, w, h, selected ? panel2 : panel);
    if (selected) border(buf, stride_px, x, y, w, h, 4, red);
    else border(buf, stride_px, x, y, w, h, 2, rgba(65,67,74,255));

    int ix=x+24, iy=y+28;
    if (iconKind==0) {
        for(int row=0; row<56; ++row) {
            int rw = row < 28 ? row : 55-row;
            rect(buf,stride_px,ix,iy+row,rw+6,1,red);
        }
    } else if(iconKind==1) {
        rect(buf,stride_px,ix,iy+12,62,42,red);
        rect(buf,stride_px,ix+6,iy+4,28,10,red);
    } else if(iconKind==2) {
        text(buf,stride_px,ix,iy,7,"N",red);
    } else {
        rect(buf,stride_px,ix+16,iy+16,32,32,red);
        rect(buf,stride_px,ix+26,iy+4,12,56,red);
        rect(buf,stride_px,ix+4,iy+26,56,12,red);
        rect(buf,stride_px,ix+23,iy+23,18,18,panel);
    }

    text(buf, stride_px, x+24, y+110, 4, title, white);
    text(buf, stride_px, x+24, y+160, 2, sub, muted);
    if(selected) rect(buf,stride_px,x+24,y+h-26,72,4,red);
}

static void draw_header(u32* buf, u32 stride_px) {
    u32 top = rgba(17,18,21,255);
    u32 red = rgba(229,9,20,255);
    u32 white = rgba(244,244,246,255);
    rect(buf,stride_px,0,0,W,92,top);
    rect(buf,stride_px,0,90,W,2,red);
    text(buf,stride_px,42,28,6,"NXFLIX",white);
    text(buf,stride_px,294,28,6,"NATIVE",red);
}

static void draw_home(u32* buf, u32 stride_px, int sel, bool appMode, const ProxyConfig* proxy) {
    u32 bg = rgba(10,10,12,255);
    u32 top = rgba(17,18,21,255);
    u32 red = rgba(229,9,20,255);
    u32 white = rgba(244,244,246,255);
    u32 muted = rgba(155,158,166,255);
    u32 green = rgba(45,190,105,255);
    rect(buf,stride_px,0,0,W,H,bg);
    draw_header(buf, stride_px);

    text(buf,stride_px,720,34,3,"HOME",white);
    text(buf,stride_px,820,34,3,"SEARCH",muted);
    text(buf,stride_px,964,34,3,"MY LIST",muted);
    text(buf,stride_px,1106,34,3,"SETTINGS",muted);

    text(buf,stride_px,42,130,6,"NATIVE SWITCH CLIENT",white);
    text(buf,stride_px,44,188,3,"WILIWILI STYLE ARCHITECTURE",muted);

    rect(buf,stride_px,902,118,328,104,rgba(24,25,29,255));
    border(buf,stride_px,902,118,328,104,2,appMode?green:red);
    text(buf,stride_px,922,135,3,appMode?"APPLICATION MODE":"APPLET MODE",appMode?green:red);
    text(buf,stride_px,922,170,2,"PROXY",muted);
    text(buf,stride_px,1000,170,2,proxy_mode_name(proxy->mode),white);
    text(buf,stride_px,922,194,2,"NO WEBAPPLET",muted);

    int y=255, cw=278, gap=18, x0=42;
    card(buf,stride_px,x0+0*(cw+gap),y,cw,286,"DEMO STREAM","PLAYER CORE READY",sel==0,0);
    card(buf,stride_px,x0+1*(cw+gap),y,cw,286,"LOCAL MEDIA","BROWSE SD CARD",sel==1,1);
    card(buf,stride_px,x0+2*(cw+gap),y,cw,286,"NETFLIX","DRM REQUIRED",sel==2,2);
    card(buf,stride_px,x0+3*(cw+gap),y,cw,286,"NETWORK","PROXY AND TUNNEL",sel==3,3);

    rect(buf,stride_px,0,620,W,100,top);
    text(buf,stride_px,42,647,3,"D PAD MOVE",muted);
    text(buf,stride_px,400,647,3,"A OPEN",white);
    text(buf,stride_px,650,647,3,"B BACK",muted);
    text(buf,stride_px,905,647,3,"PLUS EXIT",white);
}

static void draw_detail(u32* buf, u32 stride_px, int sel) {
    u32 bg=rgba(10,10,12,255), red=rgba(229,9,20,255);
    u32 white=rgba(244,244,246,255), muted=rgba(155,158,166,255);
    rect(buf,stride_px,0,0,W,H,bg);
    draw_header(buf, stride_px);
    text(buf,stride_px,42,128,3,"B BACK",muted);

    if(sel==0) {
        text(buf,stride_px,42,190,7,"PLAYER CORE",white);
        text(buf,stride_px,42,272,4,"NATIVE PLAYER INTERFACE READY",red);
        text(buf,stride_px,42,340,3,"NEXT STEP ADD MPV FOR NON DRM STREAMS",muted);
        text(buf,stride_px,42,390,3,"NETWORK SOCKET WILL USE PROXY LAYER",muted);
    } else if(sel==1) {
        text(buf,stride_px,42,190,7,"LOCAL MEDIA",white);
        text(buf,stride_px,42,272,4,"SD CARD PROVIDER",red);
        text(buf,stride_px,42,340,3,"PATH SDMC SWITCH NXFLIX MEDIA",muted);
        text(buf,stride_px,42,390,3,"PLAYER MODULE WILL OPEN LOCAL FILES",muted);
    } else {
        text(buf,stride_px,42,190,7,"NETFLIX PROVIDER",white);
        text(buf,stride_px,42,272,4,"PROVIDER MODULE IS SEPARATE",red);
        text(buf,stride_px,42,340,3,"NETFLIX REQUIRES DRM AND DEVICE AUTH",muted);
        text(buf,stride_px,42,390,3,"PROXY SOLVES NETWORK ACCESS ONLY",muted);
        text(buf,stride_px,42,440,3,"THIS APP DOES NOT BYPASS DRM",muted);
    }

    rect(buf,stride_px,42,535,1196,2,rgba(58,60,66,255));
    text(buf,stride_px,42,575,3,"NXFLIX NATIVE V2 1  AUTHOR LINKO",muted);
    text(buf,stride_px,42,640,3,"B BACK    PLUS EXIT",white);
}

static void draw_setting_row(u32* buf, u32 stride_px, int y, const char* label,
                             const char* value, bool selected) {
    u32 panel = selected ? rgba(39,40,46,255) : rgba(25,26,30,255);
    u32 red=rgba(229,9,20,255), white=rgba(244,244,246,255), muted=rgba(155,158,166,255);
    rect(buf,stride_px,42,y,1196,62,panel);
    border(buf,stride_px,42,y,1196,62,selected?3:1,selected?red:rgba(60,62,69,255));
    text(buf,stride_px,66,y+19,3,label,selected?white:muted);
    text(buf,stride_px,600,y+19,3,value,white);
}

static void draw_proxy(u32* buf, u32 stride_px, int row, const ProxyConfig* cfg,
                       const char* status, bool net_ok) {
    u32 bg=rgba(10,10,12,255), red=rgba(229,9,20,255), white=rgba(244,244,246,255);
    u32 muted=rgba(155,158,166,255), green=rgba(45,190,105,255);
    rect(buf,stride_px,0,0,W,H,bg);
    draw_header(buf, stride_px);
    text(buf,stride_px,42,120,6,"NETWORK TUNNEL",white);
    text(buf,stride_px,44,177,2,"APP LEVEL PROXY FOR NXFLIX TRAFFIC",muted);

    char port[24];
    snprintf(port,sizeof(port),"%d",cfg->port);
    const char* host = cfg->host[0] ? cfg->host : "NOT SET";
    const char* user = cfg->username[0] ? "SET" : "NOT SET";
    const char* pass = cfg->password[0] ? "SET" : "NOT SET";

    draw_setting_row(buf,stride_px,220,"MODE",proxy_mode_name(cfg->mode),row==0);
    draw_setting_row(buf,stride_px,288,"HOST",host,row==1);
    draw_setting_row(buf,stride_px,356,"PORT",port,row==2);
    draw_setting_row(buf,stride_px,424,"USERNAME",user,row==3);
    draw_setting_row(buf,stride_px,492,"PASSWORD",pass,row==4);
    draw_setting_row(buf,stride_px,560,"TEST NETFLIX 443",net_ok?"A RUN TEST":"NETWORK OFF",row==5);

    if (status && status[0]) {
        text(buf,stride_px,42,635,2,status, strstr(status,"OK")?green:red);
    } else {
        text(buf,stride_px,42,635,2,"UP DOWN SELECT   LEFT RIGHT MODE   A EDIT TEST   X SAVE   B BACK",muted);
    }
}

static bool keyboard_edit(const char* title, const char* initial, char* out, size_t out_size) {
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, title);
    swkbdConfigSetInitialText(&kbd, initial ? initial : "");
    rc = swkbdShow(&kbd, out, out_size);
    swkbdClose(&kbd);
    return R_SUCCEEDED(rc);
}

static void edit_proxy_field(ProxyConfig* cfg, int row, char* status, size_t status_size) {
    char out[256] = {0};
    if (row == 1) {
        if (keyboard_edit("Proxy host or IP", cfg->host, out, sizeof(out)) && out[0]) {
            snprintf(cfg->host, sizeof(cfg->host), "%s", out);
            snprintf(status,status_size,"HOST SAVED");
        }
    } else if (row == 2) {
        char initial[24];
        snprintf(initial,sizeof(initial),"%d",cfg->port);
        if (keyboard_edit("Proxy port", initial, out, sizeof(out)) && out[0]) {
            int p = atoi(out);
            if (p > 0 && p <= 65535) {
                cfg->port = p;
                snprintf(status,status_size,"PORT SAVED");
            } else snprintf(status,status_size,"INVALID PORT");
        }
    } else if (row == 3) {
        if (keyboard_edit("Proxy username", cfg->username, out, sizeof(out))) {
            snprintf(cfg->username, sizeof(cfg->username), "%s", out);
            snprintf(status,status_size,"USERNAME SAVED");
        }
    } else if (row == 4) {
        if (keyboard_edit("Proxy password", cfg->password, out, sizeof(out))) {
            snprintf(cfg->password, sizeof(cfg->password), "%s", out);
            snprintf(status,status_size,"PASSWORD SAVED");
        }
    }
    proxy_config_save(cfg);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    NWindow* win = nwindowGetDefault();
    Framebuffer fb;
    Result rc = framebufferCreate(&fb, win, W, H, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) return (int)rc;
    framebufferMakeLinear(&fb);

    Result net_rc = socketInitializeDefault();
    bool net_ok = R_SUCCEEDED(net_rc);

    ProxyConfig proxy;
    proxy_config_load(&proxy);

    int selected = 0;
    int proxy_row = 0;
    ScreenMode screen = SCREEN_HOME;
    bool appMode = appletGetAppletType() == AppletType_Application;
    char proxy_status[160] = {0};

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;

        if (screen == SCREEN_HOME) {
            if ((down & HidNpadButton_Left) || (down & HidNpadButton_StickLLeft)) selected = (selected + 3) % 4;
            if ((down & HidNpadButton_Right) || (down & HidNpadButton_StickLRight)) selected = (selected + 1) % 4;
            if (down & HidNpadButton_A) {
                if (selected == 3) {
                    screen = SCREEN_PROXY;
                    proxy_status[0] = 0;
                } else screen = SCREEN_DETAIL;
            }
        } else if (screen == SCREEN_DETAIL) {
            if (down & HidNpadButton_B) screen = SCREEN_HOME;
        } else if (screen == SCREEN_PROXY) {
            if (down & HidNpadButton_B) {
                proxy_config_save(&proxy);
                screen = SCREEN_HOME;
            }
            if (down & HidNpadButton_Up) proxy_row = (proxy_row + 5) % 6;
            if (down & HidNpadButton_Down) proxy_row = (proxy_row + 1) % 6;
            if (proxy_row == 0 && ((down & HidNpadButton_Left) || (down & HidNpadButton_Right))) {
                if (down & HidNpadButton_Left) proxy.mode = (ProxyMode)((proxy.mode + 2) % 3);
                else proxy.mode = (ProxyMode)((proxy.mode + 1) % 3);
                proxy_config_save(&proxy);
                snprintf(proxy_status,sizeof(proxy_status),"MODE %s",proxy_mode_name(proxy.mode));
            }
            if (down & HidNpadButton_X) {
                if (proxy_config_save(&proxy)) snprintf(proxy_status,sizeof(proxy_status),"CONFIG SAVED");
                else snprintf(proxy_status,sizeof(proxy_status),"SAVE FAILED");
            }
            if (down & HidNpadButton_A) {
                if (proxy_row >= 1 && proxy_row <= 4) {
                    edit_proxy_field(&proxy, proxy_row, proxy_status, sizeof(proxy_status));
                } else if (proxy_row == 5) {
                    if (!net_ok) snprintf(proxy_status,sizeof(proxy_status),"NETWORK INIT FAILED");
                    else proxy_test(&proxy, proxy_status, sizeof(proxy_status));
                }
            }
        }

        u32 stride = 0;
        u32* buf = (u32*)framebufferBegin(&fb, &stride);
        u32 stride_px = stride / sizeof(u32);

        if (screen == SCREEN_HOME) draw_home(buf, stride_px, selected, appMode, &proxy);
        else if (screen == SCREEN_DETAIL) draw_detail(buf, stride_px, selected);
        else draw_proxy(buf, stride_px, proxy_row, &proxy, proxy_status, net_ok);

        framebufferEnd(&fb);
    }

    proxy_config_save(&proxy);
    if (net_ok) socketExit();
    framebufferClose(&fb);
    return 0;
}
