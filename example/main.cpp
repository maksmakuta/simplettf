#include <print>

#include "simplettf/simplettf.hpp"

int main() {
    if (const auto font = simplettf::Font::load("/usr/share/fonts/open-sans/OpenSans-Regular.ttf")) {
        const auto [units_per_em, loc_format, glyph_count] = font->getMetadata();
        std::println("units per em: {}", units_per_em);
        std::println("loc format:   {}", loc_format);
        std::println("glyphs:       {}", glyph_count);

        const auto id = font->getGlyphID(U'A');
        std::println("GlyphID of A: {}", id);
    } else {
        std::println(stderr,"Error: {}", font.error());
    }
    return 0;
}
