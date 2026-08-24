from pathlib import Path
import re

# 0.2.7: Restore icon should delete sys-ticon icon overrides directly.
# Do not copy backup icon files back, because the user wants official original game icons.

p = Path('Makefile')
s = p.read_text(encoding='utf-8')
s = re.sub(r'APP_VERSION\s*:=\s*[^\n]+', 'APP_VERSION := 0.2.7', s)
p.write_text(s, encoding='utf-8')

p = Path('source/ui.cpp')
s = p.read_text(encoding='utf-8')
s = re.sub(r'NX标题工坊 0\.2\.\d+', 'NX标题工坊 0.2.7', s)
s = s.replace('X 恢复覆盖', 'X 恢复默认')
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
        bool changed = false;

        fs::create_directories(dir, ec);
        ec.clear();

        // Restore text metadata: prefer backup config, otherwise remove only our [override_nacp] section.
        fs::path cfg = dir / "config.ini";
        fs::path backup_cfg = bdir / "config.ini";
        fs::path absent_cfg = bdir / "config.ini.absent";
        if (fs::exists(backup_cfg, ec)) {
            ec.clear();
            fs::remove(cfg, ec); // libnx/filesystem may fail copy_file(overwrite_existing) with File exist.
            ec.clear();
            fs::copy_file(backup_cfg, cfg, fs::copy_options::none, ec);
            if (ec) {
                // Fallback: keep any existing config and just strip override_nacp.
                ec.clear();
                if (fs::exists(cfg, ec)) {
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
                }
            }
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

        // Restore official icon: delete sys-ticon icon overrides directly.
        // Do NOT copy backup icon.jpg/icon174.jpg back, because that may restore another custom icon.
        auto remove_icon_override = [&](const char* name) -> bool {
            std::error_code e2;
            fs::path dst = dir / name;
            if (fs::exists(dst, e2)) {
                e2.clear();
                fs::remove(dst, e2);
                if (e2) { error = std::string("无法删除 ") + name + "：" + e2.message(); return false; }
                changed = true;
            }
            return true;
        };
        if (!remove_icon_override("icon.jpg")) return false;
        if (!remove_icon_override("icon174.jpg")) return false;

        ec.clear();
        fs::remove(dir / ".nxtitlestudio", ec);
        ec.clear();
        if (fs::exists(bdir, ec)) fs::remove_all(bdir, ec);

        // If nothing was found, still report success: the game is already default.
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
'''
    s = s[:start] + replacement
p.write_text(s, encoding='utf-8')

# More accurate user-facing messages.
p = Path('source/main.cpp')
s = p.read_text(encoding='utf-8')
s = s.replace('已恢复覆盖，重启 Switch 后生效', '已恢复默认图标/信息，重启主机后生效')
s = s.replace('恢复失败：', '恢复失败：')
p.write_text(s, encoding='utf-8')

p = Path('source/http_server.cpp')
s = p.read_text(encoding='utf-8')
s = s.replace('原始覆盖已恢复，请重启 Switch', '已恢复默认图标/信息，请重启主机')
s = s.replace('恢复成功。请重启 Switch。', '恢复成功。请重启主机刷新 HOME 菜单。')
p.write_text(s, encoding='utf-8')
