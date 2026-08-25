#include "cover.hpp"

#include <curl/curl.h>
#include <jpeglib.h>
#include <png.h>
#include <webp/decode.h>

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <vector>

static const char* CACHE_ROOT = "sdmc:/switch/NXAnime/cache";
static const char* COVER_ROOT = "sdmc:/switch/NXAnime/cache/covers";

static void ensure_dirs() {
    mkdir("sdmc:/switch/NXAnime", 0777);
    mkdir(CACHE_ROOT, 0777);
    mkdir(COVER_ROOT, 0777);
}

static bool file_exists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && st.st_size > 32;
}

static void apply_proxy(CURL* curl, const ProxyConfig& cfg) {
    if (!curl) return;
    if (cfg.mode == PROXY_MODE_DIRECT) {
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
        return;
    }
    if (!cfg.host[0] || cfg.port <= 0) return;

    char addr[384];
    std::snprintf(addr, sizeof(addr), "%s:%d", cfg.host, cfg.port);
    curl_easy_setopt(curl, CURLOPT_PROXY, addr);
    curl_easy_setopt(curl, CURLOPT_PROXYTYPE,
                     cfg.mode == PROXY_MODE_HTTP ? CURLPROXY_HTTP : CURLPROXY_SOCKS5_HOSTNAME);
    if (cfg.username[0]) {
        curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME, cfg.username);
        curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD, cfg.password);
    }
}

static size_t file_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    FILE* f = static_cast<FILE*>(userdata);
    if (!f || !ptr) return 0;
    return fwrite(ptr, size, nmemb, f);
}

static bool download_file(const ProxyConfig& proxy,
                          const std::string& url,
                          const std::string& path,
                          std::string& status) {
    if (url.empty()) {
        status = "没有封面地址";
        return false;
    }

    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        status = "无法创建封面缓存文件";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(f);
        std::remove(tmp.c_str());
        status = "封面下载 curl 初始化失败";
        return false;
    }

    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 6L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NXAnime/0.3 NintendoSwitch");
    curl_easy_setopt(curl, CURLOPT_REFERER, "https://ani.girigirilove.com/");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    apply_proxy(curl, proxy);

    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    std::fclose(f);

    if (rc != CURLE_OK || code < 200 || code >= 400) {
        std::remove(tmp.c_str());
        if (rc != CURLE_OK) {
            status = "封面下载失败: ";
            status += errbuf[0] ? errbuf : curl_easy_strerror(rc);
        } else {
            char b[64];
            std::snprintf(b, sizeof(b), "封面 HTTP %ld", code);
            status = b;
        }
        return false;
    }

    if (!file_exists(tmp)) {
        std::remove(tmp.c_str());
        status = "封面文件为空";
        return false;
    }

    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        status = "封面缓存重命名失败";
        return false;
    }
    return true;
}

static bool read_file(const std::string& path, std::vector<unsigned char>& data) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 32 * 1024 * 1024) {
        std::fclose(f);
        return false;
    }
    data.resize(static_cast<size_t>(n));
    bool ok = std::fread(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

static bool decode_webp(const std::string& path, CoverImage& out) {
    std::vector<unsigned char> data;
    if (!read_file(path, data)) return false;
    int w = 0, h = 0;
    if (!WebPGetInfo(data.data(), data.size(), &w, &h) || w <= 0 || h <= 0) return false;
    if (w > 4096 || h > 4096) return false;

    std::vector<unsigned char> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    if (!WebPDecodeRGBAInto(data.data(), data.size(), rgba.data(), rgba.size(), w * 4)) return false;

    out.width = w;
    out.height = h;
    out.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (size_t i = 0; i < out.pixels.size(); ++i) {
        const unsigned char* p = &rgba[i * 4];
        out.pixels[i] = static_cast<std::uint32_t>(p[0]) |
                        (static_cast<std::uint32_t>(p[1]) << 8) |
                        (static_cast<std::uint32_t>(p[2]) << 16) |
                        (static_cast<std::uint32_t>(p[3]) << 24);
    }
    return true;
}

struct JpegError {
    jpeg_error_mgr pub;
    jmp_buf jump;
};

static void jpeg_error_exit(j_common_ptr cinfo) {
    JpegError* err = reinterpret_cast<JpegError*>(cinfo->err);
    longjmp(err->jump, 1);
}

static bool decode_jpeg(const std::string& path, CoverImage& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    jpeg_decompress_struct cinfo{};
    JpegError jerr{};
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;
    if (setjmp(jerr.jump)) {
        jpeg_destroy_decompress(&cinfo);
        std::fclose(f);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, f);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    int w = static_cast<int>(cinfo.output_width);
    int h = static_cast<int>(cinfo.output_height);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        std::fclose(f);
        return false;
    }

    out.width = w;
    out.height = h;
    out.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    std::vector<unsigned char> row(static_cast<size_t>(w) * 3);
    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char* rp = row.data();
        jpeg_read_scanlines(&cinfo, &rp, 1);
        int y = static_cast<int>(cinfo.output_scanline) - 1;
        for (int x = 0; x < w; ++x) {
            const unsigned char* p = &row[static_cast<size_t>(x) * 3];
            out.pixels[static_cast<size_t>(y) * w + x] =
                static_cast<std::uint32_t>(p[0]) |
                (static_cast<std::uint32_t>(p[1]) << 8) |
                (static_cast<std::uint32_t>(p[2]) << 16) |
                0xFF000000u;
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    std::fclose(f);
    return true;
}

static bool decode_png(const std::string& path, CoverImage& out) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path.c_str())) return false;
    if (image.width == 0 || image.height == 0 || image.width > 4096 || image.height > 4096) {
        png_image_free(&image);
        return false;
    }

    image.format = PNG_FORMAT_RGBA;
    std::vector<unsigned char> rgba(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, rgba.data(), 0, nullptr)) {
        png_image_free(&image);
        return false;
    }

    out.width = static_cast<int>(image.width);
    out.height = static_cast<int>(image.height);
    out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
    for (size_t i = 0; i < out.pixels.size(); ++i) {
        const unsigned char* p = &rgba[i * 4];
        out.pixels[i] = static_cast<std::uint32_t>(p[0]) |
                        (static_cast<std::uint32_t>(p[1]) << 8) |
                        (static_cast<std::uint32_t>(p[2]) << 16) |
                        (static_cast<std::uint32_t>(p[3]) << 24);
    }
    png_image_free(&image);
    return true;
}

static bool decode_image(const std::string& path, CoverImage& out) {
    out.clear();
    unsigned char head[16] = {0};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    size_t got = std::fread(head, 1, sizeof(head), f);
    std::fclose(f);
    if (got < 8) return false;

    if (head[0] == 0xFF && head[1] == 0xD8) return decode_jpeg(path, out);
    if (head[0] == 0x89 && head[1] == 'P' && head[2] == 'N' && head[3] == 'G') return decode_png(path, out);
    if (got >= 12 && std::memcmp(head, "RIFF", 4) == 0 && std::memcmp(head + 8, "WEBP", 4) == 0)
        return decode_webp(path, out);
    return false;
}

bool cover_load_cached_or_download(const ProxyConfig& proxy,
                                   const std::string& id,
                                   const std::string& url,
                                   CoverImage& out,
                                   std::string& status) {
    out.clear();
    if (id.empty() || url.empty()) {
        status = "暂无封面";
        return false;
    }

    ensure_dirs();
    std::string path = std::string(COVER_ROOT) + "/" + id + ".img";
    if (file_exists(path) && decode_image(path, out)) {
        status = "封面缓存已载入";
        return true;
    }

    std::remove(path.c_str());
    if (!download_file(proxy, url, path, status)) return false;
    if (!decode_image(path, out)) {
        std::remove(path.c_str());
        status = "封面格式暂不支持";
        return false;
    }

    status = "封面下载完成";
    return true;
}

void cover_draw_fit(std::uint32_t* fb,
                    std::uint32_t stride,
                    int fbw,
                    int fbh,
                    const CoverImage& image,
                    int x,
                    int y,
                    int width,
                    int height) {
    if (!fb || !image.valid() || width <= 0 || height <= 0) return;

    double sx = static_cast<double>(width) / image.width;
    double sy = static_cast<double>(height) / image.height;
    double scale = std::min(sx, sy);
    int dw = std::max(1, static_cast<int>(image.width * scale));
    int dh = std::max(1, static_cast<int>(image.height * scale));
    int dx = x + (width - dw) / 2;
    int dy = y + (height - dh) / 2;

    for (int yy = 0; yy < dh; ++yy) {
        int ty = dy + yy;
        if (ty < 0 || ty >= fbh) continue;
        int syi = std::min(image.height - 1, yy * image.height / dh);
        for (int xx = 0; xx < dw; ++xx) {
            int tx = dx + xx;
            if (tx < 0 || tx >= fbw) continue;
            int sxi = std::min(image.width - 1, xx * image.width / dw);
            fb[static_cast<size_t>(ty) * stride + tx] =
                image.pixels[static_cast<size_t>(syi) * image.width + sxi];
        }
    }
}
