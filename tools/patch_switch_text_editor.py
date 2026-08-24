from pathlib import Path

main = Path('source/main.cpp')
s = main.read_text(encoding='utf-8')

insert_after = """static int fallback_error_screen(const std::string& error) {
"""
helper = r'''static bool switch_keyboard(const char* guide, const std::string& initial, std::string& out) {
    SwkbdConfig kbd{};
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetInitialText(&kbd, initial.c_str());
    swkbdConfigSetOkButtonText(&kbd, "确定");
    swkbdConfigSetLeftOptionalSymbolKey(&kbd, "_");
    char buf[1024]{};
    rc = swkbdShow(&kbd, buf, sizeof(buf));
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return false;
    out = buf;
    return true;
}

static bool edit_metadata_on_switch(const GameEntry& game, std::string& status) {
    std::string name = game.name;
    std::string author = game.author;
    std::string version = game.version;

    if (!switch_keyboard("游戏名称", name, name)) {
        status = "已取消本机编辑";
        return false;
    }
    if (!switch_keyboard("发行商 / 作者", author, author)) {
        status = "已取消本机编辑";
        return false;
    }
    if (!switch_keyboard("显示版本", version, version)) {
        status = "已取消本机编辑";
        return false;
    }

    std::string err;
    if (apply_override(game, name, author, version, {}, err)) {
        status = "文字修改已应用，重启 Switch 后生效";
        return true;
    }
    status = "本机编辑失败：" + err;
    return false;
}

'''
if helper.strip() not in s:
    s = s.replace(insert_after, helper + insert_after, 1)

anchor = """            if (down & HidNpadButton_X) {
                std::string err;
                if (restore_override(games[sel].title_id, err)) {
                    status = "已恢复覆盖，重启 Switch 后生效";
                } else {
                    status = "恢复失败：" + err;
                }
                redraw = true;
            }

            if (down & HidNpadButton_A) {
"""
replacement = """            if (down & HidNpadButton_X) {
                std::string err;
                if (restore_override(games[sel].title_id, err)) {
                    status = "已恢复覆盖，重启 Switch 后生效";
                } else {
                    status = "恢复失败：" + err;
                }
                redraw = true;
            }

            if (down & HidNpadButton_Y) {
                edit_metadata_on_switch(games[sel], status);
                games = load_games();
                if (sel >= (int)games.size()) sel = std::max(0, (int)games.size() - 1);
                redraw = true;
            }

            if (down & HidNpadButton_A) {
"""
if anchor not in s:
    raise SystemExit('main.cpp button anchor not found')
s = s.replace(anchor, replacement, 1)
main.write_text(s, encoding='utf-8')

ui = Path('source/ui.cpp')
s = ui.read_text(encoding='utf-8')
s = s.replace('draw_text(866, 512, "A  手机编辑", 20.0f, C_TEXT);\n        draw_text(1042, 512, "X  恢复", 20.0f, C_TEXT);', 'draw_text(866, 512, "A 手机传图", 20.0f, C_TEXT);\n        draw_text(1010, 512, "Y 本机改字", 20.0f, C_TEXT);\n        draw_text(866, 544, "X 恢复", 18.0f, C_TEXT);')
s = s.replace('draw_footer("↑↓ 选择     A 手机编辑     X 恢复覆盖     + 退出", "NX标题工坊 0.2.1 UI");', 'draw_footer("↑↓ 选择     A 手机传图     Y 本机改字     X 恢复覆盖     + 退出", "NX标题工坊 0.2.2 UI");')
s = s.replace('draw_header("手机编辑", "在同一 Wi‑Fi 下扫码，用手机修改名称、厂商、版本与图标");', 'draw_header("手机传图", "同一 Wi‑Fi 下扫码上传图标；文字也可在 Switch 端按 Y 修改");')
s = s.replace('draw_text(604, 126, "编辑当前游戏", 21.0f, C_TEXT);', 'draw_text(604, 126, "当前游戏", 21.0f, C_TEXT);')
s = s.replace('draw_text(628, 364, "2. 打开局域网页并修改内容", 19.0f, C_TEXT);', 'draw_text(628, 364, "2. 上传新图标，文字可用 Y 在本机修改", 19.0f, C_TEXT);')
s = s.replace('draw_text(628, 398, "3. 点击应用，完成后重启 Switch", 19.0f, C_TEXT);', 'draw_text(628, 398, "3. 点击应用，完成后重启 Switch", 19.0f, C_TEXT);')
ui.write_text(s, encoding='utf-8')

mk = Path('Makefile')
s = mk.read_text(encoding='utf-8')
s = s.replace('APP_VERSION := 0.2.1', 'APP_VERSION := 0.2.2')
mk.write_text(s, encoding='utf-8')
