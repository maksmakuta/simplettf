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

- [ ] Basic header and table directory parsing.
- [ ] Glyph outline extraction (Bézier curve data).
- [ ] CPU-based scanline rasterizer.
- [ ] Support for multiple `cmap` platforms (Unicode/Windows).
- [ ] Kerning table (`kern` or `GPOS`) support.

## Building

Since `simpleTTF` is a one-header library, simply include the headers in your project:

```cpp
#include <simplettf/simplettf.hpp>

int main() {
    auto font = simplettf::load_file("fonts/Roboto-Regular.ttf");
    
    if (font) {
        auto glyph = font->get_glyph('A');
        // Rasterize or process glyph data
    }
    
    return 0;
}
```

## Author & License

**Maks Makuta** (C) 2026  
Distributed under the **MIT License**. See `LICENSE` for more information.
u want to start by defining the `struct` layout for the TTF Table Directory or focus on the rasterizer logic first?