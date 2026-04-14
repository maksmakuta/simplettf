#include <fstream>
#include <print>

#include "simplettf/simplettf.hpp"

void export_to_svg(const simplettf::Glyph& glyph, const std::string& filename) {
    // We add a little padding so the glyph isn't touching the edges
    float padding = 10.0f;
    float width = glyph.bounds.width() + (padding * 2);
    float height = glyph.bounds.height() + (padding * 2);

    // SVG header with a coordinate system flip (scale 1, -1) to match TTF's Y-up
    std::string svg = std::format(
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="{} {} {} {}">)",
        glyph.bounds.min.x - padding,
        -glyph.bounds.max.y - padding, // Flip Y for the viewbox
        width,
        height
    );

    // Start the path
    svg += R"(<path d=")";

    for (size_t i = 0; i < glyph.contour_indices.size(); ++i) {
        auto contour = glyph.getContour(i);
        if (contour.empty()) continue;

        for (size_t j = 0; j < contour.size(); ++j) {
            const auto& p = contour[j];
            // We negate the Y position because SVG is Y-down
            char command = (j == 0) ? 'M' : 'L';

            // Note: This simple version treats everything as lines (L).
            // We'll handle Beziers in a second.
            svg += std::format("{} {:.2f} {:.2f} ", command, p.position.x, -p.position.y);
        }
        svg += "Z "; // Close contour
    }

    svg += R"(" fill="none" stroke="black" stroke-width="0.5"/>)";
    svg += "</svg>";

    std::ofstream out(filename);
    out << svg;
    out.close();
}

int main() {
    if (const auto font = simplettf::Font::load("/usr/share/fonts/open-sans/OpenSans-Regular.ttf")) {
        const auto metadata = font->getMetadata();
        std::println("units per em: {}", metadata.units_per_em);
        std::println("loc format:   {}", metadata.loc_format);
        std::println("glyphs:       {}", metadata.glyph_count);
        std::println("metrics:      {}", metadata.metrics_count);

        std::println("ascent:       {}", metadata.ascent);
        std::println("descent:      {}", metadata.descent);
        std::println("line_gap:     {}", metadata.line_gap);

        const auto id = font->getGlyphID(U'A');
        std::println("GlyphID of A: {}", id);

        if (const auto glyph = font->getGlyph(id,36)) {
            export_to_svg(*glyph, std::to_string(id) + ".svg");
        }else {
            std::println("Glyph {} not found: {}", id, glyph.error());
        }

    } else {
        std::println(stderr,"Error: {}", font.error());
    }
    return 0;
}
