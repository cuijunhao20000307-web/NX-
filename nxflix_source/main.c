#include <switch.h>
#include <stdio.h>
#include <string.h>

#define W 1280
#define H 720

typedef enum {
    SCREEN_HOME = 0,
    SCREEN_DETAIL = 1
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

static void draw_home(u32* buf, u32 stride_px, int sel, bool appMode) {
    u32 bg = rgba(10,10,12,255);
    u32 top = rgba(17,18,21,255);
    u32 red = rgba(229,9,20,255);
    u32 white = rgba(244,244,246,255);
    u32 muted = rgba(155,158,166,255);
    u32 green = rgba(45,190,105,255);
    rect(buf,stride_px,0,0,W,H,bg);
    rect(buf,stride_px,0,0,W,92,top);
    rect(buf,stride_px,0,90,W,2,red);

    text(buf,stride_px,42,28,6,"NXFLIX",white);
    text(buf,stride_px,294,28,6,"NATIVE",red);
    text(buf,stride_px,720,34,3,"HOME",white);
    text(buf,stride_px,820,34,3,"SEARCH",muted);
    text(buf,stride_px,964,34,3,"MY LIST",muted);
    text(buf,stride_px,1106,34,3,"SETTINGS",muted);

    text(buf,stride_px,42,130,6,"NATIVE SWITCH CLIENT",white);
    text(buf,stride_px,44,188,3,"WILIWILI STYLE ARCHITECTURE",muted);

    rect(buf,stride_px,938,125,292,84,rgba(24,25,29,255));
    border(buf,stride_px,938,125,292,84,2,appMode?green:red);
    text(buf,stride_px,958,145,3,appMode?"APPLICATION MODE":"APPLET MODE",appMode?green:red);
    text(buf,stride_px,958,177,2,"NO WEBAPPLET",muted);

    int y=255, cw=278, gap=18, x0=42;
    card(buf,stride_px,x0+0*(cw+gap),y,cw,286,"DEMO STREAM","PLAYER CORE READY",sel==0,0);
    card(buf,stride_px,x0+1*(cw+gap),y,cw,286,"LOCAL MEDIA","BROWSE SD CARD",sel==1,1);
    card(buf,stride_px,x0+2*(cw+gap),y,cw,286,"NETFLIX","DRM REQUIRED",sel==2,2);
    card(buf,stride_px,x0+3*(cw+gap),y,cw,286,"SETTINGS","APP OPTIONS",sel==3,3);

    rect(buf,stride_px,0,620,W,100,top);
    text(buf,stride_px,42,647,3,"D PAD MOVE",muted);
    text(buf,stride_px,400,647,3,"A OPEN",white);
    text(buf,stride_px,650,647,3,"B BACK",muted);
    text(buf,stride_px,905,647,3,"PLUS EXIT",white);
}

static void draw_detail(u32* buf, u32 stride_px, int sel) {
    u32 bg=rgba(10,10,12,255), top=rgba(17,18,21,255), red=rgba(229,9,20,255);
    u32 white=rgba(244,244,246,255), muted=rgba(155,158,166,255);
    rect(buf,stride_px,0,0,W,H,bg);
    rect(buf,stride_px,0,0,W,92,top);
    text(buf,stride_px,42,28,6,"NXFLIX",white);
    text(buf,stride_px,294,28,6,"NATIVE",red);
    text(buf,stride_px,42,128,3,"B BACK",muted);

    if(sel==0) {
        text(buf,stride_px,42,190,7,"PLAYER CORE",white);
        text(buf,stride_px,42,272,4,"NATIVE PLAYER INTERFACE READY",red);
        text(buf,stride_px,42,340,3,"NEXT STEP ADD MPV FOR NON DRM STREAMS",muted);
        text(buf,stride_px,42,390,3,"NO SYSTEM BROWSER IS USED",muted);
    } else if(sel==1) {
        text(buf,stride_px,42,190,7,"LOCAL MEDIA",white);
        text(buf,stride_px,42,272,4,"SD CARD PROVIDER",red);
        text(buf,stride_px,42,340,3,"PATH SDMC SWITCH NXFLIX MEDIA",muted);
        text(buf,stride_px,42,390,3,"PLAYER MODULE WILL OPEN LOCAL FILES",muted);
    } else if(sel==2) {
        text(buf,stride_px,42,190,7,"NETFLIX PROVIDER",white);
        text(buf,stride_px,42,272,4,"PROVIDER MODULE IS SEPARATE",red);
        text(buf,stride_px,42,340,3,"NETFLIX REQUIRES DRM AND DEVICE AUTH",muted);
        text(buf,stride_px,42,390,3,"THIS APP DOES NOT BYPASS DRM",muted);
        text(buf,stride_px,42,440,3,"UI AND NETWORK LAYERS REMAIN NATIVE",muted);
    } else {
        text(buf,stride_px,42,190,7,"SETTINGS",white);
        text(buf,stride_px,42,272,4,"NATIVE APP OPTIONS",red);
        text(buf,stride_px,42,340,3,"CACHE NETWORK PLAYER LANGUAGE",muted);
        text(buf,stride_px,42,390,3,"WILIWILI STYLE MODULE DESIGN",muted);
    }

    rect(buf,stride_px,42,535,1196,2,rgba(58,60,66,255));
    text(buf,stride_px,42,575,3,"NXFLIX NATIVE V2 0  AUTHOR LINKO",muted);
    text(buf,stride_px,42,640,3,"B BACK    PLUS EXIT",white);
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

    int selected = 0;
    ScreenMode screen = SCREEN_HOME;
    bool appMode = appletGetAppletType() == AppletType_Application;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;

        if (screen == SCREEN_HOME) {
            if ((down & HidNpadButton_Left) || (down & HidNpadButton_StickLLeft)) {
                selected = (selected + 3) % 4;
            }
            if ((down & HidNpadButton_Right) || (down & HidNpadButton_StickLRight)) {
                selected = (selected + 1) % 4;
            }
            if (down & HidNpadButton_A) screen = SCREEN_DETAIL;
        } else {
            if (down & HidNpadButton_B) screen = SCREEN_HOME;
        }

        u32 stride = 0;
        u32* buf = (u32*)framebufferBegin(&fb, &stride);
        u32 stride_px = stride / sizeof(u32);

        if (screen == SCREEN_HOME) draw_home(buf, stride_px, selected, appMode);
        else draw_detail(buf, stride_px, selected);

        framebufferEnd(&fb);
    }

    framebufferClose(&fb);
    return 0;
}
