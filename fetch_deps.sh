#!/usr/bin/env bash
set -euo pipefail
mkdir -p "$(dirname "$0")/third_party"
cd "$(dirname "$0")/third_party"
fetch(){
  local url="$1" out="$2"
  if [[ -f "$out" ]]; then echo "exists: $out"; return; fi
  if command -v curl >/dev/null 2>&1; then curl -L --fail --retry 3 "$url" -o "$out";
  elif command -v wget >/dev/null 2>&1; then wget -O "$out" "$url";
  else echo "Need curl or wget" >&2; exit 1; fi
}
fetch "https://raw.githubusercontent.com/nayuki/QR-Code-generator/master/cpp/qrcodegen.hpp" qrcodegen.hpp
fetch "https://raw.githubusercontent.com/nayuki/QR-Code-generator/master/cpp/qrcodegen.cpp" qrcodegen.cpp
fetch "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" stb_image.h
fetch "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" stb_image_write.h
fetch "https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h" stb_truetype.h
