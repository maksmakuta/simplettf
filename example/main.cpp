#include <iterator>
#include <fstream>
#include <format>
#include <iostream>
#include <print>
#include <chrono>

#include "simplettf/simplettf.hpp"

void export_to_svg(const simplettf::Font& font, const simplettf::Glyph& glyph, float font_size, const std::string& filename) {
    if (glyph.points.empty()) return;

    const auto& meta = font.getMetadata();
    const float scale = font_size / static_cast<float>(meta.units_per_em);

    const float ascender = static_cast<float>(meta.ascent) * scale;
    const float descender = static_cast<float>(meta.descent) * scale;
    const float padding = 20.0f;

    // 1. Pre-allocate a large buffer to prevent reallocations
    // We estimate ~64 bytes per point + header
    std::string buffer;
    buffer.reserve(glyph.points.size() * 64 + 1024);

    // 2. Header and Metric Lines
    std::format_to(std::back_inserter(buffer),
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="{:.2f} {:.2f} {:.2f} {:.2f}">)"
        R"(<line x1="0" y1="0" x2="{:.2f}" y2="0" stroke="red" stroke-width="0.5" stroke-dasharray="2"/>)"
        R"(<line x1="0" y1="{:.2f}" x2="{:.2f}" y2="{:.2f}" stroke="blue" stroke-width="0.3"/>)"
        R"(<line x1="0" y1="{:.2f}" x2="{:.2f}" y2="{:.2f}" stroke="blue" stroke-width="0.3"/>)"
        R"(<line x1="{:.2f}" y1="{:.2f}" x2="{:.2f}" y2="{:.2f}" stroke="green" stroke-width="0.5" stroke-dasharray="2"/>)"
        R"(<path d=")",
        -padding, -ascender - padding, glyph.advance + (padding * 2), (ascender - descender) + (padding * 2),
        glyph.advance,
        -ascender, glyph.advance, -ascender,
        -descender, glyph.advance, -descender,
        glyph.advance, -ascender, glyph.advance, -descender
    );

    // 3. Path Generation
    for (size_t i = 0; i < glyph.contour_indices.size(); ++i) {
        auto contour = glyph.getContour(i);
        if (contour.empty()) continue;

        // Move to start
        std::format_to(std::back_inserter(buffer), "M {:.2f} {:.2f} ",
                       contour[0].position.x, -contour[0].position.y);

        for (size_t j = 1; j < contour.size(); ++j) {
            const auto& p = contour[j];
            if (p.on_curve) {
                std::format_to(std::back_inserter(buffer), "L {:.2f} {:.2f} ",
                               p.position.x, -p.position.y);
            } else {
                // Quadratic Bezier logic
                const auto& next_p = contour[(j + 1) % contour.size()];
                if (next_p.on_curve) {
                    std::format_to(std::back_inserter(buffer), "Q {:.2f} {:.2f}, {:.2f} {:.2f} ",
                                   p.position.x, -p.position.y, next_p.position.x, -next_p.position.y);
                    j++; // Skip next point as it was our destination
                } else {
                    // Midpoint rule for consecutive off-curve points
                    float mid_x = (p.position.x + next_p.position.x) * 0.5f;
                    float mid_y = (p.position.y + next_p.position.y) * 0.5f;
                    std::format_to(std::back_inserter(buffer), "Q {:.2f} {:.2f}, {:.2f} {:.2f} ",
                                   p.position.x, -p.position.y, mid_x, -mid_y);
                }
            }
        }
        buffer += "Z ";
    }

    // 4. Footer
    buffer += R"(" fill="gray" fill-opacity="0.3" stroke="black" stroke-width="0.5"/></svg>)";

    // 5. Single write to disk
    std::ofstream(filename, std::ios::binary) << buffer;
}

void print_bitmap_ascii(const simplettf::Bitmap& bitmap) {
    const auto levels = " .:-=+*#%@";
    for (int y = 0; y < bitmap.height; ++y) {
        for (int x = 0; x < bitmap.width; ++x) {
            uint8_t val = bitmap.pixels[y * bitmap.width + x];
            char c = levels[val * 9 / 255];
            std::cout << c << c; // Print twice to balance terminal aspect ratio
        }
        std::cout << "\n";
    }
}

void save_bitmap_pgm(const simplettf::Bitmap& bitmap, const std::string& filename) {
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) return;

    // P5 = Binary Grayscale, then Width, Height, and Max Value (255)
    ofs << "P5\n" << bitmap.width << " " << bitmap.height << "\n255\n";

    // Write the raw pixel buffer
    ofs.write(reinterpret_cast<const char*>(bitmap.pixels.data()), bitmap.pixels.size());
}

int main() {
    if (const auto font = simplettf::Font::load("/usr/share/fonts/open-sans/OpenSans-Regular.ttf")) {
        const auto start = std::chrono::high_resolution_clock::now();
        if (const auto glyph = font->getGlyph(font->getGlyphID(U'W'), 64)) {
            const auto bitmap = simplettf::Font::rasterize(*glyph);
            const auto end = std::chrono::high_resolution_clock::now();
            std::println("Total Glyph Process Time: {}", end - start);
            save_bitmap_pgm(bitmap, "out.pgm");
        }

    } else {
        std::println(stderr,"Error: {}", font.error());
    }
    return 0;
}
