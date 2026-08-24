#include "qr_console.hpp"
#include "qrcodegen.hpp"
#include <cstdio>

void print_qr_console(const std::string& text) {
    using qrcodegen::QrCode;
    const QrCode qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::MEDIUM);
    const int border = 2;
    const int size = qr.getSize();
    std::printf("\n");
    for (int y = -border; y < size + border; ++y) {
        bool last = false;
        bool first = true;
        for (int x = -border; x < size + border; ++x) {
            bool black = x >= 0 && y >= 0 && x < size && y < size && qr.getModule(x, y);
            if (first || black != last) { std::printf(black ? "\x1b[40m" : "\x1b[47m"); last = black; first = false; }
            std::printf("  ");
        }
        std::printf("\x1b[0m\n");
    }
    std::printf("\n%s\n", text.c_str());
}
