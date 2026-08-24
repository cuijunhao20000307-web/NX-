from pathlib import Path

p = Path('tools/patch_realtime_icon_preview.py')
s = p.read_text(encoding='utf-8')
old = '''        if (!changed) {
            error = \"没有找到可恢复的覆盖文件。\";
            return false;
        }
        return true;'''
new = '''        // If nothing existed, treat restore as success: the title is already in default state.
        // Real errors above still return false.
        (void)changed;
        return true;'''
if old in s:
    s = s.replace(old, new)
else:
    s = s.replace('''        if (!changed) {\n            error = \"没有找到可恢复的覆盖文件。\";\n            return false;\n        }\n        return true;''', '''        (void)changed;\n        return true;''')
s = s.replace('APP_VERSION := 0.2.4', 'APP_VERSION := 0.2.5')
s = s.replace('NX标题工坊 0.2.4', 'NX标题工坊 0.2.5')
p.write_text(s, encoding='utf-8')

p = Path('source/main.cpp')
s = p.read_text(encoding='utf-8')
s = s.replace('status = "已恢复覆盖，重启 Switch 后生效";', 'status = "已恢复默认，重启 Switch 后生效";')
p.write_text(s, encoding='utf-8')

p = Path('source/http_server.cpp')
s = p.read_text(encoding='utf-8')
s = s.replace('恢复成功。请重启 Switch。', '已恢复默认。请重启 Switch。')
p.write_text(s, encoding='utf-8')
