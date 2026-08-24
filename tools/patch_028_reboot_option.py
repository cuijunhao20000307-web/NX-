from pathlib import Path
import re

# 0.2.8: add a confirmed "reboot machine" option without automatic reboot.
# R opens reboot option. A confirms. B cancels. This still calls the system reboot API only after confirmation.

p = Path('Makefile')
s = p.read_text(encoding='utf-8')
s = re.sub(r'APP_VERSION\s*:=\s*[^\n]+', 'APP_VERSION := 0.2.8', s)
p.write_text(s, encoding='utf-8')

p = Path('source/ui.cpp')
s = p.read_text(encoding='utf-8')
s = re.sub(r'NX标题工坊 0\.2\.\d+', 'NX标题工坊 0.2.8', s)
s = s.replace('+ 退出', 'R 重启选项  + 退出')
s = s.replace('长按 L+R 重启主机', 'R 重启选项')
p.write_text(s, encoding='utf-8')

p = Path('source/main.cpp')
s = p.read_text(encoding='utf-8')

helper = r'''
static void reboot_switch_system() {
    Result rc = bpcInitialize();
    if (R_SUCCEEDED(rc)) {
        bpcRebootSystem();
        bpcExit();
    }
}
'''
if 'static void reboot_switch_system' not in s:
    s = s.replace('static bool switch_local_edit(GameEntry& game, std::string& status) {', helper + '\nstatic bool switch_local_edit(GameEntry& game, std::string& status) {')

s = s.replace('    bool redraw = true;\n\n    std::unique_ptr<SelectedState> state;', '    bool redraw = true;\n    bool reboot_requested = false;\n    bool reboot_confirm = false;\n\n    std::unique_ptr<SelectedState> state;')
s = s.replace('    bool redraw = true;\n    bool reboot_requested = false;\n    int reboot_hold_frames = 0;\n\n    std::unique_ptr<SelectedState> state;', '    bool redraw = true;\n    bool reboot_requested = false;\n    bool reboot_confirm = false;\n\n    std::unique_ptr<SelectedState> state;')

old = '        if (down & HidNpadButton_Plus) break;\n\n        if (!games.empty()) {'
new = '''        if (reboot_confirm) {
            if (down & HidNpadButton_A) {
                reboot_requested = true;
                goto exit_app;
            }
            if (down & HidNpadButton_B) {
                reboot_confirm = false;
                status = "已取消重启";
                redraw = true;
            }
        }

        if (down & HidNpadButton_R) {
            reboot_confirm = true;
            status = "重启机器？按 A 确认 / B 取消";
            redraw = true;
        }

        if (down & HidNpadButton_Plus) break;

        if (!games.empty() && !reboot_confirm) {'''
if old in s:
    s = s.replace(old, new)
else:
    # fallback insert after button read
    s = s.replace('        u64 down = padGetButtonsDown(&pad);\n\n', '        u64 down = padGetButtonsDown(&pad);\n\n        if (reboot_confirm) {\n            if (down & HidNpadButton_A) { reboot_requested = true; goto exit_app; }\n            if (down & HidNpadButton_B) { reboot_confirm = false; status = "已取消重启"; redraw = true; }\n        }\n        if (down & HidNpadButton_R) { reboot_confirm = true; status = "重启机器？按 A 确认 / B 取消"; redraw = true; }\n\n', 1)

# Phone editor loop also supports R option.
s = s.replace('                            if (d & HidNpadButton_Plus) goto exit_app;\n', '''                            if (d & HidNpadButton_R) {
                                phone_status = "重启机器？按 A 确认 / B 取消";
                                phone_redraw = true;
                                bool confirm = true;
                                while (confirm && appletMainLoop()) {
                                    padUpdate(&pad);
                                    u64 c = padGetButtonsDown(&pad);
                                    if (c & HidNpadButton_A) { reboot_requested = true; goto exit_app; }
                                    if (c & HidNpadButton_B) { phone_status = "已取消重启"; phone_redraw = true; confirm = false; }
                                    svcSleepThread(8'000'000);
                                }
                            }

                            if (d & HidNpadButton_Plus) goto exit_app;
''')

s = s.replace('    ui_exit();\n    return 0;', '    ui_exit();\n    if (reboot_requested) reboot_switch_system();\n    return 0;')
p.write_text(s, encoding='utf-8')
