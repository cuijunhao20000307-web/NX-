#include "app.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fs = std::filesystem;

static std::string safe_utf8_field(const char* p, size_t n) {
    size_t len = 0;
    while (len < n && p[len]) ++len;
    return std::string(p, len);
}

std::string title_id_hex(u64 tid) {
    char b[32];
    std::snprintf(b, sizeof(b), "%016lX", (unsigned long)tid);
    return b;
}

static fs::path backup_root(u64 title_id) {
    return fs::path("sdmc:/switch/NXTitleStudio/backups") / title_id_hex(title_id);
}

static bool backup_one(const fs::path& src, const fs::path& bdir, const char* name, std::string& error) {
    fs::create_directories(bdir);
    fs::path dst = bdir / name;
    fs::path absent = bdir / (std::string(name) + ".absent");
    if (fs::exists(dst) || fs::exists(absent)) return true;
    try {
        if (fs::exists(src)) fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        else { std::ofstream m(absent, std::ios::binary); m << "absent\n"; }
        return true;
    } catch (const std::exception& e) {
        error = std::string("Backup failed for ") + name + ": " + e.what();
        return false;
    }
}

static bool backup_existing(u64 title_id, const fs::path& dir, std::string& error) {
    fs::path bdir = backup_root(title_id);
    if (!backup_one(dir / "config.ini", bdir, "config.ini", error)) return false;
    if (!backup_one(dir / "icon.jpg", bdir, "icon.jpg", error)) return false;
    if (!backup_one(dir / "icon174.jpg", bdir, "icon174.jpg", error)) return false;
    return true;
}

static bool restore_one(const fs::path& dst, const fs::path& bdir, const char* name, std::string& error) {
    try {
        fs::path src = bdir / name;
        fs::path absent = bdir / (std::string(name) + ".absent");
        if (fs::exists(src)) fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        else if (fs::exists(absent)) fs::remove(dst);
        return true;
    } catch (const std::exception& e) {
        error = std::string("Restore failed for ") + name + ": " + e.what();
        return false;
    }
}

std::vector<GameEntry> load_games() {
    std::vector<GameEntry> out;
    if (R_FAILED(nsInitialize())) return out;

    constexpr int BATCH = 64;
    NsApplicationRecord records[BATCH]{};
    s32 offset = 0;

    for (;;) {
        s32 count = 0;
        Result rc = nsListApplicationRecord(records, BATCH, offset, &count);
        if (R_FAILED(rc) || count <= 0) break;

        for (s32 i = 0; i < count; ++i) {
            auto* data = (NsApplicationControlData*)std::malloc(sizeof(NsApplicationControlData));
            if (!data) continue;
            std::memset(data, 0, sizeof(*data));
            u64 outsize = 0;
            Result cr = nsGetApplicationControlData(
                NsApplicationControlSource_Storage,
                records[i].application_id,
                data,
                sizeof(*data),
                &outsize);

            GameEntry e{};
            e.title_id = records[i].application_id;
            if (R_SUCCEEDED(cr) && outsize >= sizeof(data->nacp)) {
                NacpLanguageEntry* lang = nullptr;
                if (R_SUCCEEDED(nacpGetLanguageEntry(&data->nacp, &lang)) && lang) {
                    e.name = safe_utf8_field(lang->name, sizeof(lang->name));
                    e.author = safe_utf8_field(lang->author, sizeof(lang->author));
                }
                e.version = safe_utf8_field(data->nacp.display_version, sizeof(data->nacp.display_version));
            }
            if (e.name.empty()) e.name = title_id_hex(e.title_id);
            out.push_back(std::move(e));
            std::free(data);
        }

        offset += count;
        if (count < BATCH) break;
    }

    nsExit();
    std::sort(out.begin(), out.end(), [](const GameEntry& a, const GameEntry& b) {
        return a.name < b.name;
    });
    return out;
}

static std::string strip_override_section(const std::string& text) {
    std::istringstream in(text);
    std::ostringstream out;
    std::string line;
    bool skipping = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "[override_nacp]") {
            skipping = true;
            continue;
        }
        if (skipping && !line.empty() && line.front() == '[') {
            skipping = false;
        }
        if (!skipping) out << line << "\n";
    }
    return out.str();
}

static bool write_config(const fs::path& dir,
                         const std::string& name,
                         const std::string& author,
                         const std::string& version,
                         std::string& error) {
    if (name.size() > 512 || author.size() > 256 || version.size() > 16) {
        error = "Text too long: name<=512 bytes, author<=256, version<=16.";
        return false;
    }

    fs::create_directories(dir);
    const fs::path cfg = dir / "config.ini";
    std::string old;
    {
        std::ifstream f(cfg, std::ios::binary);
        if (f) old.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    old = strip_override_section(old);

    std::ofstream f(cfg, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "Cannot write config.ini";
        return false;
    }
    f << "[override_nacp]\n";
    if (!name.empty() || !author.empty()) {
        f << "name=" << name << "\n";
        f << "author=" << author << "\n";
    }
    if (!version.empty()) f << "display_version=" << version << "\n";
    f << "\n";
    f << old;
    return true;
}

static std::vector<unsigned char> resize_rgb(const unsigned char* src, int sw, int sh, int dw, int dh) {
    std::vector<unsigned char> dst((size_t)dw * dh * 3);
    for (int y = 0; y < dh; ++y) {
        const float gy = ((y + 0.5f) * sh / dh) - 0.5f;
        int y0 = std::max(0, std::min(sh - 1, (int)gy));
        int y1 = std::min(sh - 1, y0 + 1);
        float fy = gy - (int)gy;
        if (fy < 0) fy = 0;
        for (int x = 0; x < dw; ++x) {
            const float gx = ((x + 0.5f) * sw / dw) - 0.5f;
            int x0 = std::max(0, std::min(sw - 1, (int)gx));
            int x1 = std::min(sw - 1, x0 + 1);
            float fx = gx - (int)gx;
            if (fx < 0) fx = 0;
            for (int c = 0; c < 3; ++c) {
                float p00 = src[(y0 * sw + x0) * 3 + c];
                float p10 = src[(y0 * sw + x1) * 3 + c];
                float p01 = src[(y1 * sw + x0) * 3 + c];
                float p11 = src[(y1 * sw + x1) * 3 + c];
                float v = (p00 * (1-fx) + p10 * fx) * (1-fy) + (p01 * (1-fx) + p11 * fx) * fy;
                dst[(y * dw + x) * 3 + c] = (unsigned char)std::max(0.f, std::min(255.f, v));
            }
        }
    }
    return dst;
}

static bool encode_jpeg_limited(const fs::path& path,
                                const unsigned char* rgb,
                                int w, int h,
                                size_t max_size,
                                std::string& error) {
    for (int quality : {92, 88, 84, 80, 76, 72, 68, 64, 60, 55, 50}) {
        if (!stbi_write_jpg(path.string().c_str(), w, h, 3, rgb, quality)) {
            error = "JPEG encode failed.";
            return false;
        }
        std::error_code ec;
        auto sz = fs::file_size(path, ec);
        if (!ec && sz <= max_size) return true;
    }
    error = "JPEG is still too large after compression.";
    return false;
}

static bool write_icons(const fs::path& dir,
                        const std::vector<unsigned char>& bytes,
                        std::string& error) {
    if (bytes.empty()) return true;
    int w = 0, h = 0, ch = 0;
    unsigned char* decoded = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &ch, 3);
    if (!decoded || w <= 0 || h <= 0) {
        error = "Unsupported image. Use PNG/JPG/WebP supported by stb_image.";
        if (decoded) stbi_image_free(decoded);
        return false;
    }

    auto i256 = resize_rgb(decoded, w, h, 256, 256);
    auto i174 = resize_rgb(decoded, w, h, 174, 174);
    stbi_image_free(decoded);

    fs::create_directories(dir);
    if (!encode_jpeg_limited(dir / "icon.jpg", i256.data(), 256, 256, 102400, error)) return false;
    if (!encode_jpeg_limited(dir / "icon174.jpg", i174.data(), 174, 174, 65536, error)) return false;
    return true;
}

bool apply_override(const GameEntry& game,
                    const std::string& new_name,
                    const std::string& new_author,
                    const std::string& new_version,
                    const std::vector<unsigned char>& image_bytes,
                    std::string& error) {
    try {
        fs::path dir = fs::path("sdmc:/atmosphere/contents") / title_id_hex(game.title_id);
        fs::create_directories(dir);
        if (!backup_existing(game.title_id, dir, error)) return false;
        {
            std::ofstream marker(dir / ".nxtitlestudio", std::ios::binary | std::ios::trunc);
            if (!marker) { error = "Cannot create safety marker."; return false; }
            marker << "NXTitleStudio override\n";
        }
        if (!write_config(dir, new_name, new_author, new_version, error)) {
            std::string original_error = error, rollback_error;
            restore_override(game.title_id, rollback_error);
            error = original_error;
            return false;
        }
        if (!write_icons(dir, image_bytes, error)) {
            std::string original_error = error, rollback_error;
            restore_override(game.title_id, rollback_error);
            error = original_error;
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool restore_override(u64 title_id, std::string& error) {
    try {
        fs::path dir = fs::path("sdmc:/atmosphere/contents") / title_id_hex(title_id);
        fs::path marker = dir / ".nxtitlestudio";
        if (!fs::exists(marker)) {
            error = "No NXTitleStudio marker found; refusing to delete unrelated overrides.";
            return false;
        }

        fs::path bdir = backup_root(title_id);
        if (fs::exists(bdir)) {
            if (!restore_one(dir / "config.ini", bdir, "config.ini", error)) return false;
            if (!restore_one(dir / "icon.jpg", bdir, "icon.jpg", error)) return false;
            if (!restore_one(dir / "icon174.jpg", bdir, "icon174.jpg", error)) return false;
            fs::remove_all(bdir);
        } else {
            fs::remove(dir / "icon.jpg");
            fs::remove(dir / "icon174.jpg");
            fs::path cfg = dir / "config.ini";
            if (fs::exists(cfg)) {
                std::ifstream f(cfg, std::ios::binary);
                std::string old((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                old = strip_override_section(old);
                if (old.find_first_not_of("\r\n\t ") == std::string::npos) fs::remove(cfg);
                else { std::ofstream o(cfg, std::ios::binary | std::ios::trunc); o << old; }
            }
        }
        fs::remove(marker);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
