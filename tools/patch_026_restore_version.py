from pathlib import Path

# Force visible/app version to 0.2.6 and make restore really robust.

p = Path('Makefile')
s = p.read_text(encoding='utf-8')
import re
s = re.sub(r'APP_VERSION\s*:=\s*[^\n]+', 'APP_VERSION := 0.2.6', s)
p.write_text(s, encoding='utf-8')

p = Path('source/ui.cpp')
s = p.read_text(encoding='utf-8')
s = re.sub(r'NX标题工坊 0\.2\.\d+', 'NX标题工坊 0.2.6', s)
p.write_text(s, encoding='utf-8')

p = Path('source/app.cpp')
s = p.read_text(encoding='utf-8')
start = s.find('bool restore_override(u64 title_id, std::string& error) {')
if start >= 0:
    replacement = r'''bool restore_override(u64 title_id, std::string& error) {
    try {
        fs::path dir = fs::path("sdmc:/atmosphere/contents") / title_id_hex(title_id);
        fs::path bdir = backup_root(title_id);
        std::error_code ec;

        fs::create_directories(dir, ec);
        ec.clear();

        auto remove_no_fail = [](const fs::path& p) {
            std::error_code e;
            fs::remove(p, e);
        };

        auto copy_replace = [&](const fs::path& src, const fs::path& dst, const char* label) -> bool {
            std::error_code e;
            if (!fs::exists(src, e)) return false;
            e.clear();
            fs::remove(dst, e);       // libnx filesystem can fail overwrite if destination exists
            e.clear();
            fs::copy_file(src, dst, fs::copy_options::none, e);
            if (e) {
                error = std::string("无法还原 ") + label + "：" + e.message();
                return false;
            }
            return true;
        };

        // config.ini: prefer backup; if no backup or copy fails, safely strip only [override_nacp].
        fs::path cfg = dir / "config.ini";
        fs::path backup_cfg = bdir / "config.ini";
        fs::path absent_cfg = bdir / "config.ini.absent";
        bool config_handled = false;
        if (fs::exists(backup_cfg, ec)) {
            std::string copy_error;
            std::string saved_error;
            if (copy_replace(backup_cfg, cfg, "config.ini")) {
                config_handled = true;
            } else {
                saved_error = error;
                error.clear();
                // Fall through and strip section instead of failing restore completely.
            }
        }
        ec.clear();
        if (!config_handled) {
            if (fs::exists(absent_cfg, ec)) {
                remove_no_fail(cfg);
            } else if (fs::exists(cfg, ec)) {
                std::ifstream f(cfg, std::ios::binary);
                std::string old((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                old = strip_override_section(old);
                if (old.find_first_not_of("\r\n\t ") == std::string::npos) {
                    remove_no_fail(cfg);
                } else {
                    std::ofstream o(cfg, std::ios::binary | std::ios::trunc);
                    if (!o) { error = "无法写回 config.ini"; return false; }
                    o << old;
                }
            }
        }

        auto restore_icon = [&](const char* name) -> bool {
            fs::path dst = dir / name;
            fs::path src = bdir / name;
            std::error_code e;
            if (fs::exists(src, e)) {
                if (!copy_replace(src, dst, name)) {
                    // If backup copy fails, remove the override instead of leaving modified icon.
                    error.clear();
                    remove_no_fail(dst);
                }
            } else {
                remove_no_fail(dst);
            }
            return true;
        };

        restore_icon("icon.jpg");
        restore_icon("icon174.jpg");
        remove_no_fail(dir / ".nxtitlestudio");

        ec.clear();
        if (fs::exists(bdir, ec)) fs::remove_all(bdir, ec);

        return true; // Already-default is success.
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
'''
    s = s[:start] + replacement
p.write_text(s, encoding='utf-8')
