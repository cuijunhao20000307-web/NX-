from pathlib import Path
import re

VERSION = '0.3.1'

for path in ['Makefile', 'source/ui.cpp', 'source/http_server.cpp', 'README_CN.md']:
    p = Path(path)
    if not p.exists():
        continue
    s = p.read_text(encoding='utf-8')
    s = re.sub(r'APP_VERSION\s*:=\s*[^\n]+', f'APP_VERSION := {VERSION}', s)
    s = re.sub(r'NX标题工坊\s+0\.\d+\.\d+', f'NX标题工坊 {VERSION}', s)
    s = re.sub(r'NXTitleStudio\s*/\s*NX标题工坊\s+0\.\d+\.\d+', f'NXTitleStudio / NX标题工坊 {VERSION}', s)
    s = re.sub(r'NXTitleStudio\s+0\.\d+\.\d+', f'NXTitleStudio {VERSION}', s)
    s = re.sub(r'0\.2\.10', VERSION, s)
    s = re.sub(r'0\.2\.9', VERSION, s)
    s = re.sub(r'0\.2\.8', VERSION, s)
    s = re.sub(r'0\.2\.7', VERSION, s)
    s = re.sub(r'0\.2\.6', VERSION, s)
    s = re.sub(r'0\.2\.5', VERSION, s)
    s = re.sub(r'0\.2\.4', VERSION, s)
    s = re.sub(r'0\.2\.3', VERSION, s)
    s = re.sub(r'0\.2\.2', VERSION, s)
    s = re.sub(r'0\.2\.1', VERSION, s)
    p.write_text(s, encoding='utf-8')
