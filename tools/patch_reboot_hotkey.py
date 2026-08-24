from pathlib import Path

# Add L+R long-press system reboot hotkey. This reboots the whole Switch, not just the app.
p = Path('source/main.cpp')
s = p.read_text(encoding='utf-8')

helper = r'''
static bool update_reboot_hold(PadState& pad, int& frames) {
    u64 held = padGetButtons(&pad);
    if ((held & HidNpadButton_L) && (held & HidNpadButton_R)) {
        ++frames;
        return frames >= 250; // ~2 seconds at the app's 8ms loop sleep
    }
    frames = 0;
    return false;
}

static void reboot_switch_system() {
    Result rc = bpcInitialize();
    if (R_SUCCEEDED(rc)) {
        bpcRebootSystem();
        bpcExit();
    }
}
'''
if 'static bool update_reboot_hold' not in s:
    s = s.replace('static bool switch_local_edit(GameEntry& game, std::string& status) {', helper + '\nstatic bool switch_local_edit(GameEntry& game, std::string& status) {')

s = s.replace('    bool redraw = true;\n\n    std::unique_ptr<SelectedState> state;', '    bool redraw = true;\n    bool reboot_requested = false;\n    int reboot_hold_frames = 0;\n\n    std::unique_ptr<SelectedState> state;')

s = s.replace('        u64 down = padGetButtonsDown(&pad);\n\n        if (down & HidNpadButton_Plus) break;', '        u64 down = padGetButtonsDown(&pad);\n\n        if (update_reboot_hold(pad, reboot_hold_frames)) {\n            reboot_requested = true;\n            goto exit_app;\n        }\n\n        if (down & HidNpadButton_Plus) break;')

s = s.replace('                            u64 d = padGetButtonsDown(&pad);\n\n                            if (d & HidNpadButton_Plus) goto exit_app;', '                            u64 d = padGetButtonsDown(&pad);\n\n                            if (update_reboot_hold(pad, reboot_hold_frames)) {\n                                reboot_requested = true;\n                                goto exit_app;\n                            }\n\n                            if (d & HidNpadButton_Plus) goto exit_app;')

s = s.replace('    ui_exit();\n    return 0;', '    ui_exit();\n    if (reboot_requested) reboot_switch_system();\n    return 0;')
p.write_text(s, encoding='utf-8')

# Update footer help text so the hotkey is visible.
p = Path('source/ui.cpp')
s = p.read_text(encoding='utf-8')
s = s.replace('↑↓ 选择     A 手机编辑     X 恢复覆盖     + 退出', '↑↓ 选择  A 本机修改  Y 手机图标/网页  X 恢复覆盖  长按 L+R 重启主机  + 退出')
s = s.replace('↑↓ 选择     A 本机修改     X 恢复覆盖     + 退出', '↑↓ 选择  A 本机修改  Y 手机图标/网页  X 恢复覆盖  长按 L+R 重启主机  + 退出')
s = s.replace('B 返回游戏列表     + 退出', 'B 返回游戏列表     长按 L+R 重启主机     + 退出')
p.write_text(s, encoding='utf-8')
