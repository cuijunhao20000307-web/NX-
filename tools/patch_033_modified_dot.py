from pathlib import Path
import re

# 0.3.3: show a blue dot next to titles in the left list when sys-ticon override exists.
# Also bump visible/NACP version to 0.3.3.

# 1) Add override flag to GameEntry after realtime icon patch has added icon_jpeg.
p = Path('include/app.hpp')
s = p.read_text(encoding='utf-8')
if 'bool has_override' not in s:
    if '    std::vector<unsigned char> icon_jpeg;\n' in s:
        s = s.replace('    std::vector<unsigned char> icon_jpeg;\n', '    std::vector<unsigned char> icon_jpeg;\n    bool has_override = false;\n')
    else:
        s = s.replace('    std::string version;\n', '    std::string version;\n    bool has_override = false;\n')
p.write_text(s, encoding='utf-8')

# 2) Detect override files/config during load_games.
p = Path('source/app.cpp')
s = p.read_text(encoding='utf-8')
probe = r'''
static bool has_sys_ticon_override(u64 title_id) {
    std::error_code ec;
    fs::path dir = fs::path("sdmc:/atmosphere/contents") / title_id_hex(title_id);
    if (fs::exists(dir / "icon.jpg", ec) || fs::exists(dir / "icon174.jpg", ec)) return true;

    ec.clear();
    fs::path cfg = dir / "config.ini";
    if (!fs::exists(cfg, ec)) return false;

    std::ifstream f(cfg, std::ios::binary);
    if (!f) return false;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return text.find("[override_nacp]") != std::string::npos;
}
'''
if 'has_sys_ticon_override' not in s:
    anchor = 'static fs::path backup_root(u64 title_id) {\n    return fs::path("sdmc:/switch/NXTitleStudio/backups") / title_id_hex(title_id);\n}\n'
    s = s.replace(anchor, anchor + probe)
needle = '            if (e.name.empty()) e.name = title_id_hex(e.title_id);\n            out.push_back(std::move(e));\n'
repl = '            if (e.name.empty()) e.name = title_id_hex(e.title_id);\n            e.has_override = has_sys_ticon_override(e.title_id);\n            out.push_back(std::move(e));\n'
if 'e.has_override = has_sys_ticon_override' not in s:
    s = s.replace(needle, repl)
p.write_text(s, encoding='utf-8')

# 3) Draw blue dot at the right side of the title in the left list.
p = Path('source/ui.cpp')
s = p.read_text(encoding='utf-8')
old = '            draw_text(116, y, games[i].name, 20.0f, C_TEXT, 510);\n            draw_text(116, y + 25, title_id_hex(games[i].title_id), 13.0f, C_MUTED);\n'
new = '''            draw_text(116, y, games[i].name, 20.0f, C_TEXT, 510);
            if (games[i].has_override) {
                int dot_x = 116 + text_width(games[i].name, 20.0f) + 13;
                if (dot_x > 704) dot_x = 704;
                fill_circle(dot_x, y + 14, 5, RGBA8(70, 155, 255, 255));
            }
            draw_text(116, y + 25, title_id_hex(games[i].title_id), 13.0f, C_MUTED);
'''
if 'games[i].has_override' not in s:
    s = s.replace(old, new)

# Footer/version text.
s = re.sub(r'NX标题工坊 0\.\d+\.\d+', 'NX标题工坊 0.3.3', s)
p.write_text(s, encoding='utf-8')

# 4) NACP version.
p = Path('Makefile')
s = p.read_text(encoding='utf-8')
s = re.sub(r'APP_VERSION\s*:=\s*[^\n]+', 'APP_VERSION := 0.3.3', s)
p.write_text(s, encoding='utf-8')

# 5) README note if present.
p = Path('README_CN.md')
if p.exists():
    s = p.read_text(encoding='utf-8')
    s = re.sub(r'0\.\d+\.\d+', '0.3.3', s, count=1)
    if '蓝点' not in s:
        s += '\n\n0.3.3：左侧列表对已修改/存在 sys-ticon 覆盖的游戏显示蓝色小圆点。\n'
    p.write_text(s, encoding='utf-8')
