from pathlib import Path
import re

# 0.2.9: force visible version after all previous patches.
for path in ['Makefile', 'source/ui.cpp']:
    p = Path(path)
    if not p.exists():
        continue
    s = p.read_text(encoding='utf-8')
    if path == 'Makefile':
        s = re.sub(r'APP_VERSION\s*:=\s*[^\n]+', 'APP_VERSION := 0.2.9', s)
    else:
        s = re.sub(r'NX标题工坊 0\.2\.\d+', 'NX标题工坊 0.2.9', s)
    p.write_text(s, encoding='utf-8')
