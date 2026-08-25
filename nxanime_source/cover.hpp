#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "proxy.h"
}

struct CoverImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    bool valid() const {
        return width > 0 && height > 0 && pixels.size() == static_cast<size_t>(width) * static_cast<size_t>(height);
    }

    void clear() {
        width = 0;
        height = 0;
        pixels.clear();
    }
};

bool cover_load_cached_or_download(const ProxyConfig& proxy,
                                   const std::string& id,
                                   const std::string& url,
                                   CoverImage& out,
                                   std::string& status);

void cover_draw_fit(std::uint32_t* framebuffer,
                    std::uint32_t stride_pixels,
                    int framebuffer_width,
                    int framebuffer_height,
                    const CoverImage& image,
                    int x,
                    int y,
                    int width,
                    int height);

// Empty-catalog placeholder overload. The normal catalog path uses the
// State-aware renderer in main.cpp; this keeps the no-items path harmless.
inline void draw_cover_panel(std::uint32_t*, int, int, int, int) {}
