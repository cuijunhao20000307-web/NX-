from pathlib import Path

# 1) GameEntry carries the official application icon JPEG bytes
p = Path('include/app.hpp')
s = p.read_text(encoding='utf-8')
s = s.replace('    std::string version;\n};', '    std::string version;\n    std::vector<unsigned char> icon_jpeg;\n};')
p.write_text(s, encoding='utf-8')

# 2) Load the official icon from NsApplicationControlData alongside NACP metadata
p = Path('source/app.cpp')
s = p.read_text(encoding='utf-8')
insert = r'''
static std::vector<unsigned char> safe_icon_jpeg(const unsigned char* p, size_t n) {
    if (!p || n < 4) return {};
    size_t start = 0;
    while (start + 1 < n && !(p[start] == 0xFF && p[start + 1] == 0xD8)) ++start;
    if (start + 1 >= n) return {};
    size_t end = start + 2;
    for (size_t i = start + 2; i + 1 < n; ++i) {
        if (p[i] == 0xFF && p[i + 1] == 0xD9) { end = i + 2; break; }
    }
    if (end <= start + 2 || end > n) return {};
    return std::vector<unsigned char>(p + start, p + end);
}
'''
if 'safe_icon_jpeg' not in s:
    s = s.replace('static std::string safe_utf8_field(const char* p, size_t n) {\n    size_t len = 0;\n    while (len < n && p[len]) ++len;\n    return std::string(p, len);\n}\n', 'static std::string safe_utf8_field(const char* p, size_t n) {\n    size_t len = 0;\n    while (len < n && p[len]) ++len;\n    return std::string(p, len);\n}\n' + insert + '\n')
load_snip = '                e.version = safe_utf8_field(data->nacp.display_version, sizeof(data->nacp.display_version));\n'
load_repl = load_snip + '''                size_t icon_size = 0;
                if (outsize > sizeof(data->nacp)) {
                    icon_size = std::min((size_t)(outsize - sizeof(data->nacp)), sizeof(data->icon));
                }
                if (icon_size > 4) {
                    e.icon_jpeg = safe_icon_jpeg(reinterpret_cast<const unsigned char*>(data->icon), icon_size);
                }
'''
if 'e.icon_jpeg = safe_icon_jpeg' not in s:
    s = s.replace(load_snip, load_repl)

# Make restore default filesystem-safe: never fail just because a file is missing.
start = s.find('bool restore_override(u64 title_id, std::string& error) {')
if start >= 0:
    replacement = r'''bool restore_override(u64 title_id, std::string& error) {
    try {
        fs::path dir = fs::path("sdmc:/atmosphere/contents") / title_id_hex(title_id);
        fs::path bdir = backup_root(title_id);
        std::error_code ec;
        bool changed = false;

        fs::create_directories(dir, ec);
        ec.clear();

        // Restore config.ini from backup when available; otherwise just remove our [override_nacp] section.
        fs::path cfg = dir / "config.ini";
        fs::path backup_cfg = bdir / "config.ini";
        fs::path absent_cfg = bdir / "config.ini.absent";
        if (fs::exists(backup_cfg, ec)) {
            ec.clear();
            fs::copy_file(backup_cfg, cfg, fs::copy_options::overwrite_existing, ec);
            if (ec) { error = "无法还原 config.ini：" + ec.message(); return false; }
            changed = true;
        } else if (fs::exists(absent_cfg, ec)) {
            ec.clear();
            fs::remove(cfg, ec);
            changed = true;
        } else if (fs::exists(cfg, ec)) {
            ec.clear();
            std::ifstream f(cfg, std::ios::binary);
            std::string old((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            old = strip_override_section(old);
            if (old.find_first_not_of("\r\n\t ") == std::string::npos) {
                fs::remove(cfg, ec);
            } else {
                std::ofstream o(cfg, std::ios::binary | std::ios::trunc);
                if (!o) { error = "无法写回 config.ini"; return false; }
                o << old;
            }
            changed = true;
        }

        auto restore_or_remove = [&](const char* name) -> bool {
            std::error_code e2;
            fs::path dst = dir / name;
            fs::path src = bdir / name;
            fs::path absent = bdir / (std::string(name) + ".absent");
            if (fs::exists(src, e2)) {
                e2.clear();
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing, e2);
                if (e2) { error = std::string("无法还原 ") + name + "：" + e2.message(); return false; }
                changed = true;
            } else {
                e2.clear();
                if (fs::exists(dst, e2)) {
                    e2.clear();
                    fs::remove(dst, e2);
                    changed = true;
                } else if (fs::exists(absent, e2)) {
                    changed = true;
                }
            }
            return true;
        };

        if (!restore_or_remove("icon.jpg")) return false;
        if (!restore_or_remove("icon174.jpg")) return false;

        ec.clear();
        fs::remove(dir / ".nxtitlestudio", ec);
        ec.clear();
        if (fs::exists(bdir, ec)) fs::remove_all(bdir, ec);

        if (!changed) {
            error = "没有找到可恢复的覆盖文件。";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
'''
    s = s[:start] + replacement
p.write_text(s, encoding='utf-8')

# 3) Render the selected game's actual icon in the right-side preview box
p = Path('source/ui.cpp')
s = p.read_text(encoding='utf-8')
if '#include "stb_image.h"' not in s:
    s = s.replace('#include "qrcodegen.hpp"\n', '#include "qrcodegen.hpp"\n#include "stb_image.h"\n')
fn = r'''
bool draw_image_box(int x, int y, int w, int h, const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) return false;
    int iw = 0, ih = 0, ch = 0;
    unsigned char* img = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &iw, &ih, &ch, 4);
    if (!img || iw <= 0 || ih <= 0) {
        if (img) stbi_image_free(img);
        return false;
    }
    for (int dy = 0; dy < h; ++dy) {
        int sy = std::min(ih - 1, std::max(0, dy * ih / h));
        for (int dx = 0; dx < w; ++dx) {
            int sx = std::min(iw - 1, std::max(0, dx * iw / w));
            unsigned char* px = img + ((sy * iw + sx) * 4);
            put_pixel(x + dx, y + dy, RGBA8(px[0], px[1], px[2], 255));
        }
    }
    stbi_image_free(img);
    return true;
}
'''
if 'bool draw_image_box' not in s:
    s = s.replace('void draw_label_value(int x, int y, const char* label, const std::string& value, u32 value_color = C_TEXT) {\n    draw_text(x, y, label, 17.0f, C_MUTED);\n    draw_text(x + 116, y - 1, value, 18.0f, value_color, 250);\n}\n', 'void draw_label_value(int x, int y, const char* label, const std::string& value, u32 value_color = C_TEXT) {\n    draw_text(x, y, label, 17.0f, C_MUTED);\n    draw_text(x + 116, y - 1, value, 18.0f, value_color, 250);\n}\n' + fn + '\n')
old = '        fill_rect(848, 182, 70, 70, C_PANEL_2);\n        fill_rect(848, 182, 6, 70, C_ACCENT);\n        draw_text(869, 197, "NX", 25.0f, C_ACCENT);\n'
new = '        if (!draw_image_box(848, 182, 70, 70, game.icon_jpeg)) {\n            fill_rect(848, 182, 70, 70, C_PANEL_2);\n            draw_text(869, 197, "NX", 25.0f, C_ACCENT);\n        }\n        fill_rect(848, 182, 6, 70, C_ACCENT);\n'
s = s.replace(old, new)
s = s.replace('NX标题工坊 0.2.2', 'NX标题工坊 0.2.4')
p.write_text(s, encoding='utf-8')

# 4) Restore phone-side selected-image preview
p = Path('source/http_server.cpp')
s = p.read_text(encoding='utf-8')
s = s.replace('.hint{font-size:13px;color:#85898e;margin-top:8px}.msg', '.hint{font-size:13px;color:#85898e;margin-top:8px}.preview{display:none;margin-top:12px;width:112px;height:112px;object-fit:cover;border-radius:18px;border:1px solid #484b50;background:#08090a}.msg')
s = s.replace('<label>新图标</label><div class="filebox"><label for="img" class="filebtn" style="margin:0">选择图片</label><span id="filename" class="filename">未选择图片</span></div><input id="img" type="file" accept="image/png,image/jpeg,image/webp" hidden><div class="hint">可选，支持 PNG / JPG / WebP，最大 8 MiB</div>', '<label>新图标</label><div class="filebox"><label for="img" class="filebtn" style="margin:0">选择图片</label><span id="filename" class="filename">未选择图片</span></div><input id="img" type="file" accept="image/png,image/jpeg,image/webp" hidden><img id="preview" class="preview"><div class="hint">可选，支持 PNG / JPG / WebP，最大 8 MiB，选择后会在下方预览</div>')
s = s.replace("el('img').addEventListener('change',()=>{const f=el('img').files[0];el('filename').textContent=f?f.name:'未选择图片';});", "el('img').addEventListener('change',()=>{const f=el('img').files[0];el('filename').textContent=f?f.name:'未选择图片';const p=el('preview');if(f){p.src=URL.createObjectURL(f);p.style.display='block';}else{p.removeAttribute('src');p.style.display='none';}});")
p.write_text(s, encoding='utf-8')

# 5) Bump version used by the NACP metadata while keeping launcher-compatible ASCII title
p = Path('Makefile')
s = p.read_text(encoding='utf-8')
s = s.replace('APP_VERSION := 0.2.2', 'APP_VERSION := 0.2.4')
p.write_text(s, encoding='utf-8')
