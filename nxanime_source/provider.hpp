#pragma once

#include <string>
#include <vector>

extern "C" {
#include "proxy.h"
}

// Internal fallback used by the HOS-font renderer during compilation.
// Runtime text sizing is still supplied per draw call.
static const int px = 16;

struct AnimeItem {
    std::string id;
    std::string title;
    std::string url;
    std::string extra;
    std::string cover_url;
};

struct EpisodeItem {
    std::string label;
    std::string url;
};

struct AnimeDetail {
    std::string id;
    std::string title;
    std::string status;
    std::string description;
    std::string url;
    std::string cover_url;
    std::vector<EpisodeItem> episodes;
};

bool provider_init(std::string& status);
void provider_exit();

bool provider_fetch_home(const ProxyConfig& proxy,
                         std::vector<AnimeItem>& out,
                         std::string& status);

bool provider_search(const ProxyConfig& proxy,
                     const std::string& query,
                     std::vector<AnimeItem>& out,
                     std::string& status);

bool provider_filter(const ProxyConfig& proxy,
                     int channel_id,
                     const std::string& genre,
                     const std::string& quarter,
                     const std::string& year,
                     const std::string& language,
                     const std::string& sort_by,
                     std::vector<AnimeItem>& out,
                     std::string& status);

bool provider_fetch_detail(const ProxyConfig& proxy,
                           const AnimeItem& item,
                           AnimeDetail& out,
                           std::string& status);

bool provider_test_source(const ProxyConfig& proxy, std::string& status);
