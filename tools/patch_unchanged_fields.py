from pathlib import Path
p = Path('source/app.cpp')
s = p.read_text(encoding='utf-8')
old_sig = """static bool write_config(const fs::path& dir,
                         const std::string& name,
                         const std::string& author,
                         const std::string& version,
                         std::string& error)"""
new_sig = """static bool write_config(const fs::path& dir,
                         const GameEntry& game,
                         const std::string& name,
                         const std::string& author,
                         const std::string& version,
                         std::string& error)"""
if old_sig not in s:
    raise SystemExit('write_config signature anchor not found')
s = s.replace(old_sig, new_sig, 1)
old_body = """    f << "[override_nacp]\\n";
    if (!name.empty() || !author.empty()) {
        f << "name=" << name << "\\n";
        f << "author=" << author << "\\n";
    }
    if (!version.empty()) f << "display_version=" << version << "\\n";
    f << "\\n";
    f << old;
"""
new_body = """    const bool name_changed = name != game.name;
    const bool author_changed = author != game.author;
    const bool version_changed = version != game.version;

    if (name_changed || author_changed || version_changed) {
        f << "[override_nacp]\\n";
        if (name_changed || author_changed) {
            f << "name=" << (name_changed ? name : game.name) << "\\n";
            f << "author=" << (author_changed ? author : game.author) << "\\n";
        }
        if (version_changed) f << "display_version=" << version << "\\n";
        f << "\\n";
    }
    f << old;
"""
if old_body not in s:
    raise SystemExit('write_config body anchor not found')
s = s.replace(old_body, new_body, 1)
old_call = "write_config(dir, new_name, new_author, new_version, error)"
new_call = "write_config(dir, game, new_name, new_author, new_version, error)"
if old_call not in s:
    raise SystemExit('write_config call anchor not found')
s = s.replace(old_call, new_call, 1)
p.write_text(s, encoding='utf-8')
