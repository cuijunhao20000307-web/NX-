from pathlib import Path

# Add original application icon bytes to GameEntry.
app_h = Path("include/app.hpp")
s = app_h.read_text(encoding="utf-8")
s = s.replace(
    "    std::string version;\n};",
    "    std::string version;\n    std::vector<unsigned char> icon_jpeg;\n};",
)
app_h.write_text(s, encoding="utf-8")

# Preserve the original ControlData icon JPEG from HOME metadata.
app_cpp = Path("source/app.cpp")
s = app_cpp.read_text(encoding="utf-8")
needle = "                e.version = safe_utf8_field(data->nacp.display_version, sizeof(data->nacp.display_version));\n"
repl = needle + """                const size_t icon_off = sizeof(data->nacp);
                if (outsize > icon_off) {
                    size_t icon_size = (size_t)(outsize - icon_off);
                    if (icon_size > sizeof(data->icon)) icon_size = sizeof(data->icon);
                    const unsigned char* icon = reinterpret_cast<const unsigned char*>(data->icon);
                    while (icon_size > 0 && icon[icon_size - 1] == 0) --icon_size;
                    if (icon_size > 0) e.icon_jpeg.assign(icon, icon + icon_size);
                }
"""
if needle not in s:
    raise SystemExit("app.cpp patch anchor not found")
s = s.replace(needle, repl, 1)
app_cpp.write_text(s, encoding="utf-8")

# Render the selected game's original icon instead of the NX placeholder.
ui_cpp = Path("source/ui.cpp")
s = ui_cpp.read_text(encoding="utf-8")
s = s.replace('#include "qrcodegen.hpp"\n', '#include "qrcodegen.hpp"\n#include "stb_image.h"\n', 1)

anchor = """void draw_label_value(int x, int y, const char* label, const std::string& value, u32 value_color = C_TEXT) {
    draw_text(x, y, label, 17.0f, C_MUTED);
    draw_text(x + 116, y - 1, value, 18.0f, value_color, 250);
}

} // namespace
"""
insert = """void draw_label_value(int x, int y, const char* label, const std::string& value, u32 value_color = C_TEXT) {
    draw_text(x, y, label, 17.0f, C_MUTED);
    draw_text(x + 116, y - 1, value, 18.0f, value_color, 250);
}

void draw_image_rgba_cover(const std::vector<unsigned char>& bytes, int x, int y, int w, int h) {
    if (bytes.empty() || w <= 0 || h <= 0) return;
    int sw = 0, sh = 0, ch = 0;
    unsigned char* img = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &sw, &sh, &ch, 4);
    if (!img || sw <= 0 || sh <= 0) {
        if (img) stbi_image_free(img);
        return;
    }

    float scale = std::max((float)w / (float)sw, (float)h / (float)sh);
    float used_w = w / scale;
    float used_h = h / scale;
    float ox = ((float)sw - used_w) * 0.5f;
    float oy = ((float)sh - used_h) * 0.5f;

    for (int yy = 0; yy < h; ++yy) {
        int sy = std::clamp((int)(oy + yy / scale), 0, sh - 1);
        for (int xx = 0; xx < w; ++xx) {
            int sx = std::clamp((int)(ox + xx / scale), 0, sw - 1);
            const unsigned char* p = img + ((size_t)sy * sw + sx) * 4;
            u32 c = RGBA8(p[0], p[1], p[2], 255);
            blend_pixel(x + xx, y + yy, c, p[3]);
        }
    }

    stbi_image_free(img);
}

} // namespace
"""
if anchor not in s:
    raise SystemExit("ui.cpp function insertion anchor not found")
s = s.replace(anchor, insert, 1)

old = """        fill_rect(848, 182, 70, 70, C_PANEL_2);
        fill_rect(848, 182, 6, 70, C_ACCENT);
        draw_text(869, 197, "NX", 25.0f, C_ACCENT);
"""
new = """        fill_rect(848, 182, 70, 70, C_PANEL_2);
        fill_rect(848, 182, 6, 70, C_ACCENT);
        if (!game.icon_jpeg.empty()) {
            draw_image_rgba_cover(game.icon_jpeg, 862, 190, 48, 48);
        } else {
            draw_text(869, 197, "NX", 25.0f, C_ACCENT);
        }
"""
if old not in s:
    raise SystemExit("ui.cpp selected-icon replacement anchor not found")
s = s.replace(old, new, 1)
ui_cpp.write_text(s, encoding="utf-8")
