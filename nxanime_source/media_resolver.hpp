#pragma once

#include <string>
#include "provider.hpp"

struct MediaSource {
    std::string url;
    std::string referer;
    bool hls = false;
};

// Resolve only media URLs that are directly exposed by the public episode page.
// Deliberately does not decrypt/deobfuscate protected player payloads.
bool media_resolve_public(const ProxyConfig& proxy,
                          const EpisodeItem& episode,
                          MediaSource& out,
                          std::string& status);
