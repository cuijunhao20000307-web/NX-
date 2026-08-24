from pathlib import Path
import re

# 0.3.4: batch modification workflow.
# After editing/restoring multiple titles, do not ask for reboot after every item.
# Track pending changed titles and prompt the user to press R once after all edits.

# 1) Bump visible/NACP version.
p = Path('Makefile')
s = p.read_text(encoding='utf-8')
s = re.sub(r'APP_VERSION\s*:=\s*[^\n]+', 'APP_VERSION := 0.3.4', s)
p.write_text(s, encoding='utf-8')

p = Path('source/ui.cpp')
s = p.read_text(encoding='utf-8')
s = re.sub(r'NX标题工坊 0\.\d+\.\d+', 'NX标题工坊 0.3.4', s)
p.write_text(s, encoding='utf-8')

# 2) Track pending changes in main.cpp.
p = Path('source/main.cpp')
s = p.read_text(encoding='utf-8')

if '#include <algorithm>' not in s:
    s = s.replace('#include <switch.h>\n', '#include <switch.h>\n#include <algorithm>\n')

helper = r'''
static void mark_pending_reboot(std::vector<u64>& pending_titles, u64 title_id, std::string& status) {
    if (std::find(pending_titles.begin(), pending_titles.end(), title_id) == pending_titles.end()) {
        pending_titles.push_back(title_id);
    }
    status = "已修改 " + std::to_string(pending_titles.size()) + " 项，可继续修改，最后按 R 重启一次生效";
}

static bool looks_success_status(const std::string& s) {
    return s.find("成功") != std::string::npos ||
           s.find("已保存") != std::string::npos ||
           s.find("已应用") != std::string::npos ||
           s.find("已修改") != std::string::npos ||
           s.find("已恢复") != std::string::npos;
}
'''
if 'mark_pending_reboot' not in s:
    s = s.replace('static bool switch_local_edit(GameEntry& game, std::string& status) {', helper + '\nstatic bool switch_local_edit(GameEntry& game, std::string& status) {')

# Make the local-edit success wording neutral; the caller will show the batch message.
s = s.replace('    status = "已保存，重启 Switch 后生效";\n    return true;\n}', '    status = "已保存";\n    return true;\n}', 1)

if 'std::vector<u64> pending_titles;' not in s:
    s = s.replace('    std::string status;\n    bool redraw = true;', '    std::string status;\n    std::vector<u64> pending_titles;\n    bool redraw = true;')

# R reboot confirmation should mention how many items are waiting.
old = '''        if (down & HidNpadButton_R) {
            reboot_confirm = true;
            status = "重启机器？按 A 确认 / B 取消";
            redraw = true;
        }'''
new = '''        if (down & HidNpadButton_R) {
            reboot_confirm = true;
            if (!pending_titles.empty()) {
                status = "确认重启？" + std::to_string(pending_titles.size()) + " 项待生效，按 A 重启 / B 取消";
            } else {
                status = "重启机器？按 A 确认 / B 取消";
            }
            redraw = true;
        }'''
if old in s:
    s = s.replace(old, new)

# Cancel reboot keeps pending notice.
s = s.replace('status = "已取消重启";', 'status = pending_titles.empty() ? "已取消重启" : ("已取消重启，" + std::to_string(pending_titles.size()) + " 项仍待生效");')

# X restore: count as pending reboot too, but do not force immediate reboot.
old = '''            if (down & HidNpadButton_X) {
                std::string err;
                if (restore_override(games[sel].title_id, err)) {
                    status = "已恢复覆盖，重启 Switch 后生效";
                    games = load_games();
                    if (sel >= (int)games.size()) sel = std::max(0, (int)games.size() - 1);
                } else {
                    status = "恢复失败：" + err;
                }
                redraw = true;
            }'''
new = '''            if (down & HidNpadButton_X) {
                std::string err;
                u64 tid = games[sel].title_id;
                if (restore_override(tid, err)) {
                    mark_pending_reboot(pending_titles, tid, status);
                    games = load_games();
                    if (sel >= (int)games.size()) sel = std::max(0, (int)games.size() - 1);
                } else {
                    status = "恢复失败：" + err;
                }
                redraw = true;
            }'''
if old in s:
    s = s.replace(old, new)

# A local edit: mark pending and refresh list so the blue dot appears immediately.
old = '''            if (down & HidNpadButton_A) {
                switch_local_edit(games[sel], status);
                redraw = true;
            }'''
new = '''            if (down & HidNpadButton_A) {
                u64 tid = games[sel].title_id;
                if (switch_local_edit(games[sel], status)) {
                    mark_pending_reboot(pending_titles, tid, status);
                    games = load_games();
                    if (sel >= (int)games.size()) sel = std::max(0, (int)games.size() - 1);
                }
                redraw = true;
            }'''
if old in s:
    s = s.replace(old, new)

# Phone editor: if the phone-side operation succeeded, mark this title as pending too.
old = '''                        status = phone_status;
                        games = load_games();
                        if (sel >= (int)games.size()) sel = std::max(0, (int)games.size() - 1);
                        redraw = true;'''
new = '''                        status = phone_status;
                        if (looks_success_status(status)) {
                            mark_pending_reboot(pending_titles, games[sel].title_id, status);
                        }
                        games = load_games();
                        if (sel >= (int)games.size()) sel = std::max(0, (int)games.size() - 1);
                        redraw = true;'''
if old in s:
    s = s.replace(old, new)

p.write_text(s, encoding='utf-8')

# 3) README note.
p = Path('README_CN.md')
if p.exists():
    s = p.read_text(encoding='utf-8')
    s = re.sub(r'0\.\d+\.\d+', '0.3.4', s, count=1)
    if '批量修改' not in s:
        s += '\n\n0.3.4：支持批量修改多个游戏，全部改完后按 R 重启一次让所有修改一起生效。\n'
    p.write_text(s, encoding='utf-8')
