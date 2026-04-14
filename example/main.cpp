#include <fstream>
#include <print>

#include "simplettf/simplettf.hpp"

void export_to_svg(const simplettf::Glyph& glyph, const std::string& filename) {
    if (glyph.points.empty()) return;

    // ViewBox: [min_x, min_y, width, height]
    // We negate Y and use -max.y as the top because SVG's Y increases downwards
    std::string svg = std::format(
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="{:.2f} {:.2f} {:.2f} {:.2f}">)",
        glyph.bounds.min.x - 5,
        -glyph.bounds.max.y - 5,
        glyph.bounds.width() + 10,
        glyph.bounds.height() + 10
    );

    svg += R"(<path d=")";

    for (size_t i = 0; i < glyph.contour_indices.size(); ++i) {
        auto contour = glyph.getContour(i);
        if (contour.empty()) continue;

        // Move to the first point
        svg += std::format("M {:.2f} {:.2f} ", contour[0].position.x, -contour[0].position.y);

        for (size_t j = 1; j < contour.size(); ++j) {
            const auto& p = contour[j];

            if (p.on_curve) {
                // Regular line
                svg += std::format("L {:.2f} {:.2f} ", p.position.x, -p.position.y);
            } else {
                // It's a control point for a Quadratic Bezier (Q)
                // We need the NEXT point to be the destination
                size_t next_idx = (j + 1) % contour.size();
                const auto& next_p = contour[next_idx];

                if (next_p.on_curve) {
                    svg += std::format("Q {:.2f} {:.2f}, {:.2f} {:.2f} ",
                        p.position.x, -p.position.y,
                        next_p.position.x, -next_p.position.y);
                } else {
                    // Two off-curve points in a row: find the midpoint
                    float mid_x = (p.position.x + next_p.position.x) / 2.0f;
                    float mid_y = (p.position.y + next_p.position.y) / 2.0f;
                    svg += std::format("Q {:.2f} {:.2f}, {:.2f} {:.2f} ",
                        p.position.x, -p.position.y,
                        mid_x, -mid_y);
                }
            }
        }
        svg += "Z "; // Close the contour
    }

    svg += R"(" fill="red" stroke="black" stroke-width="0.5"/>)";
    svg += "</svg>";

    std::ofstream(filename) << svg;
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

        const auto id = font->getGlyphID(U'J');
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
