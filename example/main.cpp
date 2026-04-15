#include <fstream>
#include <print>
#include <string>
#include <chrono>

#include "simplettf/simplettf.hpp"

void save_bitmap_pgm(const simplettf::Bitmap& bitmap, const std::string& filename) {
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) return;
    ofs << "P5\n" << bitmap.width << " " << bitmap.height << "\n255\n";
    ofs.write(reinterpret_cast<const char*>(bitmap.pixels.data()), bitmap.pixels.size());
}

int main() {
    if (const auto font = simplettf::Font::load("/usr/share/fonts/open-sans/OpenSans-Regular.ttf")) {
        const auto start = std::chrono::high_resolution_clock::now();
        if (const auto glyph = font->getGlyph(font->getGlyphID(U'R'), 24)) {
            const auto bitmap = simplettf::Font::rasterizeSDF(*glyph);
            const auto end = std::chrono::high_resolution_clock::now();
            std::println("Total Glyph Process Time: {}", end - start);
            save_bitmap_pgm(bitmap, "out.pgm");
        }

        const auto a = font->getGlyphID(U'V');
        const auto b = font->getGlyphID(U'A');
        const auto kerning = font->getKerning(a,b,32);
        std::println("Kerning: {}", kerning);
    } else {
        std::println(stderr,"Error: {}", font.error());
    }
    return 0;
}
