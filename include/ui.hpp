#pragma once

#include <string>
#include <vector>
#include "app.hpp"

bool ui_init(std::string& error);
void ui_exit();

void ui_draw_game_list(const std::vector<GameEntry>& games,
                       int selected,
                       const std::string& status);

void ui_draw_phone_editor(const GameEntry& game,
                          const std::string& url,
                          const std::string& status);
