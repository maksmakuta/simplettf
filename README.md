# simpleTTF

A modern, lightweight C++23 library for loading and rasterizing TrueType (.ttf) and OpenType (.otf) fonts. Designed for developers who need high-performance font handling without the overhead of FreeType.

## Features

* **Zero External Dependencies:** Built from the ground up using standard C++23.
* **Modern API:** Utilizes `std::span`, `std::expected`, and `std::optional` for a safe, modern developer experience.
* **CPU Rasterization:** High-quality software rasterizer for generating glyph bitmaps directly to memory.
* **Efficient Parsing:** Fast binary parsing of TTF/OTF tables (head, maxp, loca, glyf, cmap).
* **Metrics & Kerning:** Easy access to horizontal metrics, advances, and kerning pairs.
* **Memory Efficient:** Minimal memory footprint, suitable for embedded systems or high-performance game engines.

## Roadmap

- [X] Basic header and table directory parsing.
- [X] Glyph outline extraction (Bézier curve data).
- [ ] CPU-based scanline rasterizer.
- [X] Support for multiple `cmap` platforms (Unicode/Windows).
- [ ] Kerning table (`kern` or `GPOS`) support.

## Building

Since `simpleTTF` is a one-header library, simply include the headers in your project:

```cpp
#include <simplettf/simplettf.hpp>
#include <print>

int main() {
    if (const auto font = simplettf::Font::load("/path/to/font.ttf")) {
        const auto id = font->getGlyphID(U'A');
        std::println("GlyphID of A: {}", id);

        if (const auto glyph = font->getGlyph(id,36)) { // 36 is a size
            // do with raw data what do you want
        }else {
            std::println(stderr,"Error: {}", font.error());
        }

    } else {
        std::println(stderr,"Error: {}", font.error());
    }
    return 0;
}
```

## Author & License

**Maks Makuta** (C) 2026  
Distributed under the **MIT License**. See `LICENSE` for more information.