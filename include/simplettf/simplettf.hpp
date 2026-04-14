#ifndef SIMPLETTF_HPP
#define SIMPLETTF_HPP

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace simplettf {

    namespace internal {

        class BufferReader;

        enum GlyphFlags : uint8_t {
            ON_CURVE_POINT                          = 0x01,
            X_SHORT_VECTOR                          = 0x02,
            Y_SHORT_VECTOR                          = 0x04,
            REPEAT_FLAG                             = 0x08,
            X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR    = 0x10,
            Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR    = 0x20,
            OVERLAP_SIMPLE                          = 0x40
        };

        struct TableInfo final {
            std::uint32_t tag;
            std::uint32_t offset;
            std::uint32_t size;
        };

        struct CMapSegment final {
            uint32_t start_code;
            uint32_t end_code;
            uint32_t start_glyph_id;
            int16_t  id_delta;
            uint16_t id_range_offset;
            uint32_t offset_in_file;

            bool operator < (const uint32_t code) const { return end_code < code; }
        };

        struct GlyphDataRange final {
            uint32_t offset;
            uint32_t length;
        };

        std::uint32_t as_tag(const std::string& tag);
        std::string to_string(std::uint32_t tag);

    }

    struct Metadata final {
        uint32_t units_per_em{0};
        uint32_t loc_format{0};
        uint32_t glyph_count{0};
        uint16_t metrics_count{0};

        int16_t ascent{0};
        int16_t descent{0};
        int16_t line_gap{0};
    };

    struct Vec2 final {
        float x{0.0f}, y{0.0f};
    };

    struct BoundingBox final {
        Vec2 min{0.0f, 0.0f};
        Vec2 max{0.0f, 0.0f};

        [[nodiscard]] float width() const  { return max.x - min.x; }
        [[nodiscard]] float height() const { return max.y - min.y; }
        [[nodiscard]] Vec2  size() const   { return { width(), height() }; }

        [[nodiscard]] bool contains(const Vec2 p) const {
            return p.x >= min.x && p.x <= max.x &&
                   p.y >= min.y && p.y <= max.y;
        }
    };

    struct PathPoint final {
        Vec2 position;
        bool on_curve{false}; // True = Line/Anchor, False = Quadratic Control Point
    };

    struct Glyph final {
        std::vector<PathPoint> points;
        std::vector<uint32_t> contour_indices;
        BoundingBox bounds;
        float advance{0.0f};
        float lsb{0.0f};

        [[nodiscard]] std::span<const PathPoint> getContour(size_t index) const;
    };

    struct Bitmap {
        uint32_t width, height;
        std::vector<uint8_t> pixels;

        void set_pixel(const uint32_t x, const uint32_t y, const uint8_t val) {
            if (x < width && y < height) pixels[y * width + x] = val;
        }
    };

    using GlyphID = std::uint32_t;

    class Font final {
    public:
        static std::expected<Font,std::string> load(const std::filesystem::path& path);

        [[nodiscard]] Metadata getMetadata() const;
        [[nodiscard]] float getLineHeight(float fontSize) const;
        [[nodiscard]] GlyphID getGlyphID(uint32_t codepoint) const;
        [[nodiscard]] std::expected<Glyph,std::string> getGlyph(GlyphID glyphID, float size = 1.f) const;

        [[nodiscard]] static Bitmap rasterize(const Glyph& glyph);
        [[nodiscard]] Bitmap rasterizeSDF(const Glyph& glyph) const;

    private:
        void loadTables();
        void populateGlyphCache();
        void parseFormat12(internal::BufferReader &reader);
        void parseFormat4(internal::BufferReader &reader);
        void getGlyphMetrics(GlyphID gid, Glyph &glyph, float scale) const;

        static void getSimpleGlyph(internal::BufferReader& reader, Glyph& glyph, short num_contours, float scale);

        [[nodiscard]] internal::GlyphDataRange getGlyphDataRange(GlyphID id) const;
        [[nodiscard]] const internal::TableInfo *findTable(const std::string& tag) const;
        [[nodiscard]] std::optional<internal::BufferReader> getReaderFor(const std::string& tag) const;

        Metadata m_metadata;
        std::vector<std::byte> m_font_data;
        std::vector<internal::TableInfo> m_tables;
        std::vector<internal::CMapSegment> m_segments;
    };

}

#endif //SIMPLETTF_HPP
