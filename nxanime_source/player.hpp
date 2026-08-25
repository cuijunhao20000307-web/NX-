#pragma once

#include <switch.h>
#include <string>
#include "media_resolver.hpp"

extern "C" {
#include "proxy.h"
}

enum PlayerResult {
    PLAYER_BACK = 0,
    PLAYER_ENDED = 1,
    PLAYER_ERROR = 2,
};

PlayerResult player_play(Framebuffer* framebuffer,
                         PadState* pad,
                         const ProxyConfig& proxy,
                         const MediaSource& source,
                         const std::string& title,
                         std::string& status);
