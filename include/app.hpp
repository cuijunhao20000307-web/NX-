#pragma once
#include <switch.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct GameEntry {
    u64 title_id = 0;
    std::string name;
    std::string author;
    std::string version;
};

struct SelectedState {
    GameEntry game;
    std::atomic<bool> server_running{false};
    std::atomic<bool> changed{false};
    std::mutex mutex;
    std::string status;
};

std::vector<GameEntry> load_games();
bool apply_override(const GameEntry& game,
                    const std::string& new_name,
                    const std::string& new_author,
                    const std::string& new_version,
                    const std::vector<unsigned char>& image_bytes,
                    std::string& error);
bool restore_override(u64 title_id, std::string& error);
std::string title_id_hex(u64 tid);
