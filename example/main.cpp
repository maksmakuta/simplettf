#include <print>

#include "simplettf/simplettf.hpp"

int main() {
    if (const auto font = simplettf::Font::load("/usr/share/fonts/open-sans/OpenSans-Regular.ttf")) {
        const auto metadata = font->getMetadata();
        std::println("units per em: {}", metadata.units_per_em);
        std::println("loc format:   {}", metadata.loc_format);
        std::println("glyphs:       {}", metadata.glyph_count);
    } else {
        std::println(stderr,"Error: {}", font.error());
    }
    return 0;
}
