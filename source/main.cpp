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
    std::printf("NX标题工坊 UI 初始化失败。\n\n%s\n\n按 + 退出。\n", error.c_str());
    consoleUpdate(nullptr);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
    return 1;
}

static bool edit_text_field(const char* title,
                            const char* guide,
                            const std::string& initial,
                            char* out,
                            size_t out_size) {
    if (!out || out_size == 0) return false;
    SwkbdConfig kbd{};
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, title);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetInitialText(&kbd, initial.c_str());
    Result rc = swkbdShow(&kbd, out, out_size);
    swkbdClose(&kbd);
    return R_SUCCEEDED(rc);
}

static bool switch_local_edit(GameEntry& game, std::string& status) {
    char name[768]{};
    char author[384]{};
    char version[64]{};

    if (!edit_text_field("游戏名称", "不想修改就直接按 OK，取消则不保存", game.name, name, sizeof(name))) {
        status = "已取消本机修改";
        return false;
    }
    if (!edit_text_field("发行商 / 作者", "不想修改就直接按 OK，取消则不保存", game.author, author, sizeof(author))) {
        status = "已取消本机修改";
        return false;
    }
    if (!edit_text_field("显示版本", "不想修改就直接按 OK，取消则不保存", game.version, version, sizeof(version))) {
        status = "已取消本机修改";
        return false;
    }

    std::string new_name = name[0] ? std::string(name) : game.name;
    std::string new_author = author[0] ? std::string(author) : game.author;
    std::string new_version = version[0] ? std::string(version) : game.version;

    if (new_name == game.name && new_author == game.author && new_version == game.version) {
        status = "没有修改内容，保持原样";
        return false;
    }

    std::string err;
    std::vector<unsigned char> no_icon;
    if (!apply_override(game, new_name, new_author, new_version, no_icon, err)) {
        status = "保存失败：" + err;
        return false;
    }

    game.name = new_name;
    game.author = new_author;
    game.version = new_version;
    status = "已保存，重启 Switch 后生效";
    return true;
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
                    games = load_games();
                    if (sel >= (int)games.size()) sel = std::max(0, (int)games.size() - 1);
                } else {
                    status = "恢复失败：" + err;
                }
                redraw = true;
            }

            if (down & HidNpadButton_A) {
                switch_local_edit(games[sel], status);
                redraw = true;
            }

            if (down & HidNpadButton_Y) {
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
                        games = load_games();
                        if (sel >= (int)games.size()) sel = std::max(0, (int)games.size() - 1);
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
