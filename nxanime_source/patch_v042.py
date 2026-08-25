from pathlib import Path
import re

MOBILE_UA = 'Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Mobile Safari/537.36'

# Make all NXAnime-side HTTP requests look like Android Chrome.
for rel in ['provider.cpp', 'media_resolver.cpp', 'player.cpp']:
    path = Path('nxanime_source') / rel
    s = path.read_text(encoding='utf-8')
    s = re.sub(r'NXAnime/[0-9.]+ NintendoSwitch', MOBILE_UA, s)
    s = s.replace('Mozilla/5.0 (Nintendo Switch; NXAnime) AppleWebKit/605.1.15 Version/17.0 Safari/605.1.15', MOBILE_UA)
    path.write_text(s, encoding='utf-8')

# Add mobile-oriented browser flags to the HOS WebApplet fallback.
main = Path('nxanime_source/main.cpp')
m = main.read_text(encoding='utf-8')

# BootAsMediaPlayer caused the official "feature unavailable" route on some systems;
# keep this as a normal web page applet for this experiment.
m = m.replace('                    if(hosversionAtLeast(2,0,0))webConfigSetBootAsMediaPlayer(&webcfg,true);\n', '')

needle = '''                    webConfigSetDisplayUrlKind(&webcfg,false);\n'''
extra = '''                    webConfigSetDisplayUrlKind(&webcfg,false);\n                    if(hosversionAtLeast(3,0,0))webConfigSetJsExtension(&webcfg,true);\n                    if(hosversionAtLeast(4,0,0)){\n                        webConfigSetTouchEnabledOnContents(&webcfg,true);\n                        webConfigSetUserAgentAdditionalString(&webcfg,"Android 13; Pixel 7; Mobile; Chrome/124");\n                        webConfigSetPageCache(&webcfg,true);\n                        webConfigSetWebAudio(&webcfg,true);\n                    }\n                    if(hosversionAtLeast(6,0,0))webConfigSetMediaAutoPlay(&webcfg,true);\n'''
if needle not in m:
    raise SystemExit('WebApplet display-url config marker not found')
m = m.replace(needle, extra, 1)

m = m.replace('v0.4.1', 'v0.4.2')
m = m.replace('NXAnime 0.4.1 startup', 'NXAnime 0.4.2 startup')
m = m.replace('SAFE BOOT 0.4.1', 'SAFE BOOT 0.4.2')

main.write_text(m, encoding='utf-8')
print('v0.4.2 Android mobile identity patch applied')
