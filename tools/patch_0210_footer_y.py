from pathlib import Path
import re

# 0.2.10: make Y phone editor visible in footer and quick actions.

# Version metadata
p = Path('Makefile')
s = p.read_text(encoding='utf-8')
s = re.sub(r'APP_VERSION\s*:=\s*[^\n]+', 'APP_VERSION := 0.2.10', s)
p.write_text(s, encoding='utf-8')

# UI text
p = Path('source/ui.cpp')
s = p.read_text(encoding='utf-8')

# Force version label
s = re.sub(r'NX标题工坊 0\.2\.\d+', 'NX标题工坊 0.2.10', s)
s = s.replace('NX标题工坊 0.2.1', 'NX标题工坊 0.2.10')

# Quick action block: show A/Y/X/R clearly in two rows.
quick_old = '''        fill_rect(848, 466, 364, 96, C_PANEL_2);
        draw_text(866, 480, "快捷操作", 17.0f, C_MUTED);
        draw_text(866, 512, "A  手机编辑", 20.0f, C_TEXT);
        draw_text(1042, 512, "X  恢复", 20.0f, C_TEXT);
'''
quick_new = '''        fill_rect(848, 466, 364, 96, C_PANEL_2);
        draw_text(866, 480, "快捷操作", 17.0f, C_MUTED);
        draw_text(866, 510, "A 本机修改", 18.0f, C_TEXT);
        draw_text(1010, 510, "Y 手机编辑", 18.0f, C_TEXT);
        draw_text(866, 536, "X 恢复", 18.0f, C_TEXT);
        draw_text(1010, 536, "R 重启", 18.0f, C_TEXT);
'''
s = s.replace(quick_old, quick_new)

# Handle if previous scripts already changed A label.
s = s.replace('draw_text(866, 512, "A  本机修改", 20.0f, C_TEXT);\n        draw_text(1042, 512, "X  恢复", 20.0f, C_TEXT);',
              'draw_text(866, 510, "A 本机修改", 18.0f, C_TEXT);\n        draw_text(1010, 510, "Y 手机编辑", 18.0f, C_TEXT);\n        draw_text(866, 536, "X 恢复", 18.0f, C_TEXT);\n        draw_text(1010, 536, "R 重启", 18.0f, C_TEXT);')
s = s.replace('draw_text(866, 512, "A 本机修改", 20.0f, C_TEXT);\n        draw_text(1042, 512, "X  恢复", 20.0f, C_TEXT);',
              'draw_text(866, 510, "A 本机修改", 18.0f, C_TEXT);\n        draw_text(1010, 510, "Y 手机编辑", 18.0f, C_TEXT);\n        draw_text(866, 536, "X 恢复", 18.0f, C_TEXT);\n        draw_text(1010, 536, "R 重启", 18.0f, C_TEXT);')

# Footer: keep it short enough to fit on 720p screen.
s = re.sub(r'draw_footer\("[^"]*A[^\n]*\+ 退出",\s*"NX标题工坊 [^"]*"\);',
           'draw_footer("↑↓ 选择  A 本机改  Y 手机改  X 恢复  R 重启  + 退出", "NX标题工坊 0.2.10");',
           s)
s = s.replace('draw_footer("↑↓ 选择     A 手机编辑     X 恢复覆盖     + 退出", "NX标题工坊 0.2.1");',
              'draw_footer("↑↓ 选择  A 本机改  Y 手机改  X 恢复  R 重启  + 退出", "NX标题工坊 0.2.10");')

p.write_text(s, encoding='utf-8')
