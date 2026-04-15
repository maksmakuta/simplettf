# simpleTTF

A modern, lightweight C++23 library for loading and rasterizing TrueType (.ttf) and OpenType (.otf) fonts. Designed for developers who need high-performance font handling without the overhead of FreeType.

---
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
- [X] CPU-based scanline rasterizer.
- [X] Support for multiple `cmap` platforms (Unicode/Windows).
- [ ] Kerning table (`kern` or `GPOS`) support.
- [ ] Extend .otf file support
- [X] SDF bitmap rendering

---

## Building

`simpleTTF` is a header-plus-implementation library. You can either include the source directly in your project or use CMake to build it as a static library.

### Prerequisites
* **Compiler:** A modern C++23 compliant compiler (GCC 13+, Clang 16+, or MSVC 19.34+).
* **Build System:** CMake 3.20 or higher.

### Integration via CMake
Add the following to your `CMakeLists.txt`:

```cmake
# Add simpleTTF to your project
add_library(simplettf STATIC 
    path/to/simplettf.cpp
)

# Ensure C++23 is enabled
target_compile_features(simplettf PUBLIC cxx_std_23)
target_include_directories(simplettf PUBLIC path/to/include)
```

---

## Usage

### 1. Loading a Font
`simpleTTF` uses `std::expected` for error handling, making it easy to catch issues like missing files or corrupt headers without exceptions.

```cpp
#include <simplettf/simplettf.hpp>

int main() {
    auto font_result = simplettf::Font::load("path/to/font.ttf");

    if (!font_result) {
        std::cerr << "Failed to load font: " << font_result.error() << std::endl;
        return -1;
    }

    const auto& font = font_result.value();
    // ...
}
```

### 2. Basic Rasterization
The workflow involves mapping a codepoint to a `GlyphID`, fetching the geometry, and then rasterizing it into a grayscale bitmap.

```cpp
// 1. Get Glyph ID from a Unicode codepoint
simplettf::GlyphID gid = font.getGlyphID('M');

// 2. Fetch Glyph data at a specific size (scale)
auto glyph_result = font.getGlyph(gid, 32.0f);

if (glyph_result) {
    const auto& glyph = glyph_result.value();

    // 3. Rasterize to a grayscale bitmap (8-bit alpha)
    simplettf::Bitmap bitmap = font.rasterize(glyph);

    // Use bitmap.pixels (std::vector<uint8_t>), bitmap.width, and bitmap.height
    // Each pixel is an alpha value from 0 to 255.
}
```

### 3. Font Metrics
You can easily calculate line spacing and layout for text rendering.

```cpp
float fontSize = 18.0f;
float lineHeight = font.getLineHeight(fontSize);

auto metadata = font.getMetadata();
std::cout << "Units Per EM: " << metadata.units_per_em << std::endl;
```

---

### Author & License

**Maks Makuta** (C) 2026   
Distributed under the **MIT License**.  
See `LICENSE` for more information.  