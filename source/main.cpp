#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "app.hpp"
#include "http_server.hpp"
#include "qr_console.hpp"

static void draw_list(const std::vector<GameEntry>& games, int sel, const std::string& status) {
    consoleClear();
    std::printf("NXTitleStudio 0.1.0\n");
    std::printf("Safe metadata/icon override for Atmosphere + sys-ticon\n\n");
    if (games.empty()) {
        std::printf("No applications found.\n");
        std::printf("+ Exit\n");
        return;
    }
    const int page = 16;
    int start = (sel / page) * page;
    int end = std::min((int)games.size(), start + page);
    for (int i = start; i < end; ++i) {
        std::printf("%c %s\n", i == sel ? '>' : ' ', games[i].name.c_str());
        if (i == sel) std::printf("  %s\n", title_id_hex(games[i].title_id).c_str());
    }
    std::printf("\nD-Pad: select   A: phone editor   X: restore   +: exit\n");
    if (!status.empty()) std::printf("Status: %s\n", status.c_str());
}

int main(int argc, char** argv) {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    Result sr = socketInitializeDefault();
    bool net_ok = R_SUCCEEDED(sr);

    auto games = load_games();
    int sel = 0;
    std::string status;
    bool redraw = true;

    std::unique_ptr<SelectedState> state;
    std::unique_ptr<HttpServer> server;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        if (down & HidNpadButton_Plus) break;
        if (!games.empty()) {
            if (down & HidNpadButton_Up) { sel = (sel - 1 + (int)games.size()) % (int)games.size(); redraw = true; }
            if (down & HidNpadButton_Down) { sel = (sel + 1) % (int)games.size(); redraw = true; }

            if (down & HidNpadButton_X) {
                std::string err;
                if (restore_override(games[sel].title_id, err)) status = "Override removed. Reboot Switch.";
                else status = "Restore failed: " + err;
                redraw = true;
            }

            if (down & HidNpadButton_A) {
                if (!net_ok) {
                    status = "Network service unavailable. Connect Wi-Fi and reopen app.";
                    redraw = true;
                } else {
                    if (server) server->stop();
                    state = std::make_unique<SelectedState>();
                    state->game = games[sel];
                    server = std::make_unique<HttpServer>(*state);
                    if (!server->start(8080)) {
                        status = "Cannot get LAN IP. Make sure Wi-Fi is connected.";
                        redraw = true;
                    } else {
                        consoleClear();
                        std::printf("Selected: %s\n", games[sel].name.c_str());
                        std::printf("Scan this QR with your phone (same Wi-Fi):\n");
                        print_qr_console(server->url());
                        std::printf("B: back to game list    +: exit\n");
                        consoleUpdate(nullptr);

                        while (appletMainLoop()) {
                            padUpdate(&pad);
                            u64 d = padGetButtonsDown(&pad);
                            if (d & HidNpadButton_Plus) goto exit_app;
                            if (d & HidNpadButton_B) break;
                            if (state->changed.exchange(false)) {
                                std::lock_guard<std::mutex> lk(state->mutex);
                                consoleClear();
                                std::printf("Selected: %s\n", games[sel].name.c_str());
                                print_qr_console(server->url());
                                std::printf("B: back to list    +: exit\n\n%s\n", state->status.c_str());
                            }
                            consoleUpdate(nullptr);
                        }
                        server->stop();
                        server.reset();
                        state.reset();
                        redraw = true;
                    }
                }
            }
        }

        if (redraw) {
            draw_list(games, sel, status);
            redraw = false;
        }
        consoleUpdate(nullptr);
    }

exit_app:
    if (server) server->stop();
    if (net_ok) socketExit();
    consoleExit(nullptr);
    return 0;
}
