#include <switch.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "app.hpp"
#include "http_server.hpp"
#include "ui.hpp"

static int fallback_error_screen(const std::string& error) {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    std::printf("NXTitleStudio UI init failed.\n\n%s\n\nPress + to exit.\n", error.c_str());
    consoleUpdate(nullptr);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
    return 1;
}

int main(int argc, char** argv) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    std::string ui_error;
    if (!ui_init(ui_error)) return fallback_error_screen(ui_error);

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
            if (down & HidNpadButton_Up) {
                sel = (sel - 1 + (int)games.size()) % (int)games.size();
                status.clear();
                redraw = true;
            }
            if (down & HidNpadButton_Down) {
                sel = (sel + 1) % (int)games.size();
                status.clear();
                redraw = true;
            }

            if (down & HidNpadButton_X) {
                std::string err;
                if (restore_override(games[sel].title_id, err)) {
                    status = "已恢复覆盖，重启 Switch 后生效";
                } else {
                    status = "恢复失败: " + err;
                }
                redraw = true;
            }

            if (down & HidNpadButton_A) {
                if (!net_ok) {
                    status = "网络不可用，请连接 Wi‑Fi 后重新打开";
                    redraw = true;
                } else {
                    if (server) server->stop();
                    state = std::make_unique<SelectedState>();
                    state->game = games[sel];
                    server = std::make_unique<HttpServer>(*state);

                    if (!server->start(8080)) {
                        status = "无法获取局域网 IP，请检查 Wi‑Fi";
                        redraw = true;
                    } else {
                        std::string phone_status;
                        ui_draw_phone_editor(games[sel], server->url(), phone_status);

                        bool phone_redraw = false;
                        while (appletMainLoop()) {
                            padUpdate(&pad);
                            u64 d = padGetButtonsDown(&pad);

                            if (d & HidNpadButton_Plus) goto exit_app;
                            if (d & HidNpadButton_B) break;

                            if (state->changed.exchange(false)) {
                                std::lock_guard<std::mutex> lk(state->mutex);
                                phone_status = state->status;
                                phone_redraw = true;
                            }

                            if (phone_redraw) {
                                ui_draw_phone_editor(games[sel], server->url(), phone_status);
                                phone_redraw = false;
                            }
                            svcSleepThread(8'000'000);
                        }

                        server->stop();
                        server.reset();
                        state.reset();
                        status = phone_status;
                        redraw = true;
                    }
                }
            }
        }

        if (redraw) {
            ui_draw_game_list(games, sel, status);
            redraw = false;
        }
        svcSleepThread(8'000'000);
    }

exit_app:
    if (server) server->stop();
    if (net_ok) socketExit();
    ui_exit();
    return 0;
}
