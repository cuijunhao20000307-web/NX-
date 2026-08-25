#pragma once

#include <string>
#include <vector>

extern "C" {
#include "proxy.h"
}

struct AnimeItem {
    std::string id;
    std::string title;
    std::string url;
    std::string extra;
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
    std::vector<EpisodeItem> episodes;
};

bool provider_init(std::string& status);
void provider_exit();

bool provider_fetch_home(const ProxyConfig& proxy,
                         std::vector<AnimeItem>& out,
                         std::string& status);

bool provider_fetch_detail(const ProxyConfig& proxy,
                           const AnimeItem& item,
                           AnimeDetail& out,
                           std::string& status);

bool provider_test_source(const ProxyConfig& proxy, std::string& status);
