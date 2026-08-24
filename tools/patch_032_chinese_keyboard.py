from pathlib import Path
import re

# NXTitleStudio 0.3.2: use Simplified Chinese software keyboard for local title/author editing.

# Bump metadata version.
p = Path('Makefile')
s = p.read_text(encoding='utf-8')
s = re.sub(r'APP_VERSION\s*:=\s*[^\n]+', 'APP_VERSION := 0.3.2', s)
p.write_text(s, encoding='utf-8')

# Patch local keyboard type and visible version text.
p = Path('source/main.cpp')
s = p.read_text(encoding='utf-8')
old_sig = '''static bool edit_text_field(const char* title,
                            const char* guide,
                            const std::string& initial,
                            char* out,
                            size_t out_size) {'''
new_sig = '''static bool edit_text_field(const char* title,
                            const char* guide,
                            const std::string& initial,
                            char* out,
                            size_t out_size,
                            SwkbdType type = SwkbdType_ZhHans) {'''
s = s.replace(old_sig, new_sig)
old = '''    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, title);'''
new = '''    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetType(&kbd, type);
    swkbdConfigSetHeaderText(&kbd, title);'''
s = s.replace(old, new)

# Name and author use Chinese keyboard; version stays QWERTY for numbers/letters.
s = s.replace('edit_text_field("显示版本", "不想修改就直接按 OK，取消则不保存", game.version, version, sizeof(version))',
              'edit_text_field("显示版本", "不想修改就直接按 OK，取消则不保存", game.version, version, sizeof(version), SwkbdType_QWERTY)')
p.write_text(s, encoding='utf-8')

p = Path('source/ui.cpp')
s = p.read_text(encoding='utf-8')
s = re.sub(r'NX标题工坊 0\.\d+\.\d+', 'NX标题工坊 0.3.2', s)
s = s.replace('↑↓ 选择  A 本机改  Y 手机改  X 恢复  R 重启  + 退出',
              '↑↓ 选择  A 本机改  Y 手机改  X 恢复  R 重启  + 退出')
p.write_text(s, encoding='utf-8')
