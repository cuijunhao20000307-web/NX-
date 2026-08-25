#include <stdio.h>
#include <switch.h>

#define NETFLIX_HOME  "https://www.netflix.com/"
#define NETFLIX_LOGIN "https://www.netflix.com/login"

static bool is_application_mode(void) {
    return appletGetAppletType() == AppletType_Application;
}

static Result open_netflix(const char *url) {
    WebCommonConfig config;
    Result rc = webNewsCreate(&config, url);
    if (R_FAILED(rc)) return rc;

    webConfigSetPointer(&config, true);
    webConfigSetFooter(&config, true);
    return webConfigShow(&config, NULL);
}

static void draw_screen(Result last_rc, bool attempted) {
    consoleClear();
    printf("\x1b[1;1H");
    printf("NXFlix Web Launcher v1.0.0\n");
    printf("Unofficial Netflix WebApplet launcher\n\n");

    if (is_application_mode()) {
        printf("Mode: Application Mode [OK]\n\n");
        printf("[A] Open Netflix\n");
        printf("[X] Open Netflix Login\n");
    } else {
        printf("Mode: Applet Mode [LIMITED]\n\n");
        printf("WebApplet cannot reliably start from Album mode.\n");
        printf("Hold R while opening a game, enter hbmenu,\n");
        printf("then launch NXFlix Web again.\n");
    }

    printf("\n[+] Exit\n");
    printf("\nNote: This only opens Netflix in the Switch system browser.\n");
    printf("Netflix video playback may fail if required DRM/EME is unavailable.\n");

    if (attempted) {
        printf("\nLast WebApplet result: 0x%08X\n", last_rc);
    }
    consoleUpdate(NULL);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    Result last_rc = 0;
    bool attempted = false;
    draw_screen(last_rc, attempted);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        if (down & HidNpadButton_Plus) break;

        if (is_application_mode() && (down & HidNpadButton_A)) {
            attempted = true;
            last_rc = open_netflix(NETFLIX_HOME);
            draw_screen(last_rc, attempted);
        }

        if (is_application_mode() && (down & HidNpadButton_X)) {
            attempted = true;
            last_rc = open_netflix(NETFLIX_LOGIN);
            draw_screen(last_rc, attempted);
        }

        consoleUpdate(NULL);
    }

    consoleExit(NULL);
    return 0;
}
