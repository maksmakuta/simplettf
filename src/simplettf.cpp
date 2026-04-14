#include "simplettf/simplettf.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <print>
#include <span>

namespace simplettf {

    namespace internal {

        class BufferReader final {
        public:
            explicit BufferReader(const std::span<const std::byte> data) : m_data(data) {}

            template <typename T>
            requires std::is_arithmetic_v<T>
            T read() {
                T value = peek<T>(m_offset);
                m_offset += sizeof(T);
                return value;
            }

            template <typename T>
            requires std::is_arithmetic_v<T>
            [[nodiscard]] T peek(const size_t offset) const {
                if (offset + sizeof(T) > m_data.size()) {
                    throw std::runtime_error("BufferReader: Out of bounds peek");
                }

                T value;
                std::memcpy(&value, m_data.data() + offset, sizeof(T));

                if constexpr (std::endian::native == std::endian::little) {
                    return std::byteswap(value);
                }
                return value;
            }

            float read_fixed() {
                const auto val = read<int32_t>();
                return static_cast<float>(val) / 65536.0f;
            }

            [[nodiscard]] std::span<const std::byte> view_slice(const size_t offset, const size_t length) const {
                if (offset + length > m_data.size()) {
                    throw std::runtime_error("BufferReader: Slice out of bounds");
                }
                return m_data.subspan(offset, length);
            }

            void seek(const size_t position) {
                if (position > m_data.size()) {
                    throw std::runtime_error("BufferReader: Seek out of bounds");
                }
                m_offset = position;
            }

            void skip(const size_t bytes) { seek(m_offset + bytes); }

            [[nodiscard]] size_t tell() const { return m_offset; }
            [[nodiscard]] size_t remaining() const { return m_data.size() - m_offset; }

        private:
            std::span<const std::byte> m_data;
            size_t m_offset = 0;
        };

        std::uint32_t as_tag(const std::string& tag) {
            if (tag.size() != 4) {
                return 0;
            }
            return static_cast<std::uint32_t>(static_cast<unsigned char>(tag[0])) << 24 |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(tag[1])) << 16 |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(tag[2])) <<  8 |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(tag[3]));
        }

        std::string to_string(const std::uint32_t tag) {
            std::string result;
            result.resize_and_overwrite(4, [tag](char* buf, size_t) {
                buf[0] = static_cast<char>(tag >> 24 & 0xFF);
                buf[1] = static_cast<char>(tag >> 16 & 0xFF);
                buf[2] = static_cast<char>(tag >> 8  & 0xFF);
                buf[3] = static_cast<char>(tag       & 0xFF);
                return 4;
            });
            return result;
        }

    }

    [[nodiscard]] std::span<const PathPoint> Glyph::getContour(const size_t index) const {
        if (index >= contour_indices.size()) return {};
        const uint32_t start = index == 0 ? 0 : contour_indices[index - 1] + 1;
        const uint32_t end = contour_indices[index];
        if (end < start || end >= points.size()) return {};
        return {points.data() + start, end - start + 1};
    }

    std::expected<Font, std::string> Font::load(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            return std::unexpected{"File not found: " + path.string()};
        }

        std::ifstream ifs(path, std::ios::binary | std::ios::ate);
        if (!ifs) {
            return std::unexpected{"Failed to open file: " + path.string()};
        }

        const auto fileSize = static_cast<size_t>(ifs.tellg());
        ifs.seekg(0, std::ios::beg);

        Font font;
        font.m_font_data.resize(fileSize);

        if (!ifs.read(reinterpret_cast<char*>(font.m_font_data.data()), static_cast<std::streamsize>(fileSize))) {
            return std::unexpected{"Failed to read font data"};
        }

        font.loadTables();
        font.populateGlyphCache();

        return font;
    }

    const internal::TableInfo* Font::findTable(const std::string& tag) const {
        const auto t = internal::as_tag(tag);
        const auto it = std::ranges::find_if(m_tables, [&](const auto& info) {
            return info.tag == t;
        });
        return it != m_tables.end() ? &*it : nullptr;
    }

    std::optional<internal::BufferReader> Font::getReaderFor(const std::string &tag) const {
        const auto* table = findTable(tag);
        if (!table) {
            return std::nullopt;
        }
        return internal::BufferReader(std::span(m_font_data).subspan(table->offset, table->size));
    }

    Metadata Font::getMetadata() const {
        return m_metadata;
    }

    void Font::populateGlyphCache() {
        auto reader_opt = getReaderFor("cmap");
        if (!reader_opt) return;
        auto& reader = *reader_opt;

        reader.skip(2);
        const auto numTables = reader.read<uint16_t>();

        uint32_t format4Offset = 0;
        uint32_t format12Offset = 0;

        for (int i = 0; i < numTables; ++i) {
            const auto platformID = reader.read<uint16_t>();
            const auto encodingID = reader.read<uint16_t>();
            const auto offset = reader.read<uint32_t>();

            // Platform 3 (Windows), Encoding 1 (Unicode BMP) -> Format 4 usually
            if (platformID == 3 && encodingID == 1) {
                format4Offset = offset;
            }
            // Platform 3 (Windows), Encoding 10 (Unicode Full) -> Format 12 usually
            else if (platformID == 3 && encodingID == 10) {
                format12Offset = offset;
            }
            // Also common: Platform 0 (Unicode Standard)
            else if (platformID == 0) {
                if (encodingID == 3 || encodingID == 4) format12Offset = offset;
                else format4Offset = offset;
            }
        }

        if (format12Offset != 0) {
            reader.seek(format12Offset);
            parseFormat12(reader);
        } else if (format4Offset != 0) {
            reader.seek(format4Offset);
            parseFormat4(reader);
        }
    }

    void Font::parseFormat12(internal::BufferReader& reader) {
        reader.skip(10); // format, reserved, length, language
        const auto numGroups = reader.read<uint32_t>();

        for (uint32_t i = 0; i < numGroups; ++i) {
            m_segments.push_back({
                .start_code = reader.read<uint32_t>(),
                .end_code = reader.read<uint32_t>(),
                .start_glyph_id = reader.read<uint32_t>(),
                .id_range_offset = 0xFFFF // Sentinel for Format 12
            });
        }
    }
    void Font::parseFormat4(internal::BufferReader& reader) {
        // format (2), length (2), language (2) = 6 bytes
        reader.skip(6);

        const uint16_t segCount = reader.read<uint16_t>() / 2;
        reader.skip(6); // searchRange, entrySelector, rangeShift

        std::vector<uint16_t> ends(segCount);
        for (auto& v : ends) v = reader.read<uint16_t>();

        reader.skip(2); // reservedPad

        std::vector<uint16_t> starts(segCount);
        for (auto& v : starts) v = reader.read<uint16_t>();

        std::vector<int16_t> deltas(segCount);
        for (auto& v : deltas) v = reader.read<int16_t>();

        for (uint16_t i = 0; i < segCount; ++i) {
            const auto currentOffsetPos = static_cast<uint32_t>(reader.tell());
            const auto rangeOffset = reader.read<uint16_t>();

            m_segments.push_back({
                .start_code = starts[i],
                .end_code = ends[i],
                .id_delta = deltas[i],
                .id_range_offset = rangeOffset,
                .offset_in_file = currentOffsetPos
            });
        }
    }

    void Font::getSimpleGlyph(
        internal::BufferReader &reader,
        Glyph &glyph,
        const short num_contours,
        const float scale
    ) {
        if (num_contours == 0) return;

        glyph.contour_indices.resize(static_cast<std::size_t>(num_contours));
        for (auto& end : glyph.contour_indices) {
            end = reader.read<uint16_t>();
        }

        const uint32_t total_points = glyph.contour_indices.back() + 1;
        glyph.points.resize(total_points);

        const auto instr_len = reader.read<uint16_t>();
        reader.skip(instr_len);

        std::vector<uint8_t> flags(total_points);
        for (uint32_t i = 0; i < total_points; ++i) {
            const auto f = reader.read<uint8_t>();
            flags[i] = f;
            if (f & internal::REPEAT_FLAG) {
                auto count = reader.read<uint8_t>();
                while (count-- > 0) flags[++i] = f;
            }
        }

        int32_t x = 0;
        for (uint32_t i = 0; i < total_points; ++i) {
            const uint8_t f = flags[i];
            if (f & internal::X_SHORT_VECTOR) {
                const auto val = reader.read<uint8_t>();
                x += f & internal::X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR ? val : -val;
            } else if (!(f & internal::X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR)) {
                x += reader.read<int16_t>();
            }
            glyph.points[i].position.x = static_cast<float>(x) * scale;
            glyph.points[i].on_curve = f & internal::ON_CURVE_POINT;
        }

        int32_t y = 0;
        for (uint32_t i = 0; i < total_points; ++i) {
            if (const uint8_t f = flags[i]; f & internal::Y_SHORT_VECTOR) {
                const auto val = reader.read<uint8_t>();
                y += f & internal::Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR ? val : -val;
            } else if (!(f & internal::Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR)) {
                y += reader.read<int16_t>();
            }
            glyph.points[i].position.y = static_cast<float>(y) * scale;
        }
    }

    GlyphID Font::getGlyphID(const uint32_t codepoint) const {
        const auto it = std::ranges::lower_bound(
            m_segments,
            codepoint,
            std::ranges::less{},
            &internal::CMapSegment::end_code
        );

        if (it == m_segments.end() || codepoint < it->start_code) {
            return 0;
        }

        if (it->id_range_offset == 0xFFFF) {
            return it->start_glyph_id + (codepoint - it->start_code);
        }

        if (it->id_range_offset == 0) {
            return codepoint + it->id_delta & 0xFFFF;
        }

        const internal::BufferReader reader(m_font_data);
        const uint32_t glyph_offset = it->offset_in_file + it->id_range_offset + (codepoint - it->start_code) * 2;

        if (const auto glyph_id = reader.peek<uint16_t>(glyph_offset); glyph_id != 0) {
            return glyph_id + it->id_delta & 0xFFFF;
        }

        return 0;
    }

    std::expected<Glyph,std::string> Font::getGlyph(const GlyphID glyphID, const float size) const {
        if (size <= 0)
            return std::unexpected{"size cannot be negative or equal to zero"};

        const float scale = size / static_cast<float>(m_metadata.units_per_em);
        Glyph glyph{};
        getGlyphMetrics(glyphID, glyph, scale);
        auto [offset, length] = getGlyphDataRange(glyphID);
        if (length == 0) {
            return glyph;
        }

        auto glyf_opt = getReaderFor("glyf");
        if (!glyf_opt) return std::unexpected("Missing 'glyf' table");

        auto& reader = *glyf_opt;
        reader.seek(offset);

        const auto num_contours = reader.read<int16_t>();

        glyph.bounds.min.x = static_cast<float>(reader.read<int16_t>()) * scale;
        glyph.bounds.min.y = static_cast<float>(reader.read<int16_t>()) * scale;
        glyph.bounds.max.x = static_cast<float>(reader.read<int16_t>()) * scale;
        glyph.bounds.max.y = static_cast<float>(reader.read<int16_t>()) * scale;

        if (num_contours > 0) {
            getSimpleGlyph(reader, glyph, num_contours, scale);
        } else {
            return std::unexpected("Composite glyphs not supported yet");
        }

        return glyph;
    }

    void Font::getGlyphMetrics(const GlyphID gid, Glyph& glyph, const float scale) const {
        // 1. Safety check: Never try to read past the total glyph count in the font
        if (gid >= m_metadata.glyph_count) {
            glyph.advance = 0.0f;
            glyph.lsb = 0.0f;
            return;
        }

        auto reader_opt = getReaderFor("hmtx");
        if (!reader_opt) return;
        auto& reader = *reader_opt;

        uint16_t raw_advance = 0;
        int16_t raw_lsb = 0;

        if (gid < m_metadata.metrics_count) {
            // Simple case: glyph has both unique advance and unique LSB
            // Entry size is 4 bytes (2 advance + 2 lsb)
            reader.seek(gid * 4);
            raw_advance = reader.read<uint16_t>();
            raw_lsb = reader.read<int16_t>();
        } else {
            // Monospaced/Extended case: glyph shares the last available advance width
            // but has its own entry in the trailing LSB array.

            // Read the last valid advance width from the hMetrics array
            reader.seek((m_metadata.metrics_count - 1) * 4);
            raw_advance = reader.read<uint16_t>();

            // Calculate the position in the additional LSB-only array
            // Offset = (Full hMetrics array) + (Index into LSB array * 2 bytes)
            uint32_t lsb_offset = (m_metadata.metrics_count * 4) + ((gid - m_metadata.metrics_count) * 2);

            reader.seek(lsb_offset);
            raw_lsb = reader.read<int16_t>();
        }

        glyph.advance = static_cast<float>(raw_advance) * scale;
        glyph.lsb = static_cast<float>(raw_lsb) * scale;
    }

    void Font::loadTables() {
        internal::BufferReader reader(m_font_data);
        reader.skip(4);
        const auto numTables = reader.read<uint16_t>();
        reader.skip(6);

        for (int i = 0; i < numTables; ++i) {
            internal::TableInfo info{};
            info.tag = reader.read<uint32_t>();
            reader.skip(4);
            info.offset = reader.read<uint32_t>();
            info.size   = reader.read<uint32_t>();
            m_tables.push_back(info);
        }

        if (auto headReader = getReaderFor("head")) {
            headReader->seek(18);
            m_metadata.units_per_em = headReader->read<uint16_t>();

            headReader->seek(50);
            m_metadata.loc_format = headReader->read<int16_t>();
        }

        if (auto maxpReader = getReaderFor("maxp")) {
            maxpReader->seek(4);
            m_metadata.glyph_count = maxpReader->read<uint16_t>();
        }

        if (auto hheaReader = getReaderFor("hhea")) {
            hheaReader->skip(4);
            m_metadata.ascent = hheaReader->read<int16_t>();
            m_metadata.descent = hheaReader->read<int16_t>();
            m_metadata.line_gap = hheaReader->read<int16_t>();

            hheaReader->seek(34);
            m_metadata.metrics_count = hheaReader->read<uint16_t>();
        }

    }

    internal::GlyphDataRange Font::getGlyphDataRange(const GlyphID id) const {
        const auto reader_opt = getReaderFor("loca");
        if (!reader_opt || id >= m_metadata.glyph_count) {
            return {0, 0};
        }

        auto& reader = *reader_opt;
        uint32_t start = 0;
        uint32_t end = 0;

        if (m_metadata.loc_format == 0) {
            start = static_cast<uint32_t>(reader.peek<uint16_t>(id * 2)) * 2;
            end   = static_cast<uint32_t>(reader.peek<uint16_t>((id + 1) * 2)) * 2;
        } else {
            start = reader.peek<uint32_t>(id * 4);
            end   = reader.peek<uint32_t>((id + 1) * 4);
        }

        return { .offset = start, .length = end - start };
    }

    float Font::getLineHeight(const float fontSize) const {
        const float scale = fontSize / static_cast<float>(m_metadata.units_per_em);
        return static_cast<float>(m_metadata.ascent - m_metadata.descent + m_metadata.line_gap) * scale;
    }

    struct Segment { Vec2 p0, p1; };

    void flatten_quad(Vec2 p0, Vec2 p1, Vec2 p2, std::vector<Segment>& out, float tol = 0.1f) {
        float dx = (p0.x + p2.x) * 0.5f - p1.x;
        float dy = (p0.y + p2.y) * 0.5f - p1.y;
        if (dx*dx + dy*dy > tol) {
            Vec2 p01 = { (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
            Vec2 p12 = { (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };
            Vec2 mid = { (p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f };
            flatten_quad(p0, p01, mid, out, tol);
            flatten_quad(mid, p12, p2, out, tol);
        } else {
            out.push_back({p0, p2});
        }
    }

    struct Cell {
    float cover; // Vertical delta
    float area;  // Area-weighted delta
};

    // Recursive subdivision for Quadratic Beziers
void flatten_quadratic(Vec2 p0, Vec2 p1, Vec2 p2, std::vector<Segment>& out, float tol) {
    // Calculate how far the midpoint of the curve is from the chord
    float dx = (p0.x + p2.x) * 0.5f - p1.x;
    float dy = (p0.y + p2.y) * 0.5f - p1.y;

    if (dx*dx + dy*dy > tol) {
        Vec2 p01 = { (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
        Vec2 p12 = { (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };
        Vec2 mid = { (p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f };

        flatten_quadratic(p0, p01, mid, out, tol);
        flatten_quadratic(mid, p12, p2, out, tol);
    } else {
        out.push_back({p0, p2});
    }
}

std::vector<Segment> flatten(const Glyph& glyph, float tolerance) {
    std::vector<Segment> segments;

    for (size_t i = 0; i < glyph.contour_indices.size(); ++i) {
        auto contour = glyph.getContour(i); // Assuming this returns std::vector<GlyphPoint>
        if (contour.empty()) continue;

        Vec2 start_point = contour[0].position;
        Vec2 current = start_point;

        for (size_t j = 1; j < contour.size(); ++j) {
            if (contour[j].on_curve) {
                segments.push_back({current, contour[j].position});
                current = contour[j].position;
            } else {
                // Quadratic Bezier logic
                Vec2 p1 = contour[j].position;
                Vec2 p2;

                size_t next_idx = (j + 1) % contour.size();
                if (contour[next_idx].on_curve) {
                    p2 = contour[next_idx].position;
                    j++; // Skip the next point as we consumed it as p2
                } else {
                    // Implied midpoint rule: If two off-curve points are consecutive,
                    // there is a virtual on-curve point exactly in the middle.
                    p2 = { (p1.x + contour[next_idx].position.x) * 0.5f,
                           (p1.y + contour[next_idx].position.y) * 0.5f };
                }

                flatten_quadratic(current, p1, p2, segments, tolerance);
                current = p2;
            }
        }

        // Always close the contour
        if (current.x != start_point.x || current.y != start_point.y) {
            segments.push_back({current, start_point});
        }
    }
    return segments;
}

struct Rasterizer {
    int width, height;
    std::vector<Cell> cells;

    Rasterizer(int w, int h) : width(w), height(h), cells(w * h, {0, 0}) {}

    void add_line(Vec2 p0, Vec2 p1) {
        if (std::abs(p0.y - p1.y) < 1e-6f) return;

        // 1. Direction: 1 for down, -1 for up
        const float dir = (p1.y > p0.y) ? 1.0f : -1.0f;
        if (p0.y > p1.y) std::swap(p0, p1);

        // 2. Vertical Clipping
        if (p1.y <= 0 || p0.y >= height) return;

        const float dxdy = (p1.x - p0.x) / (p1.y - p0.y);
        float x = p0.x;

        const int y_start = std::max(0, static_cast<int>(std::floor(p0.y)));
        const int y_end   = std::min(height - 1, static_cast<int>(std::floor(p1.y)));

        for (int y = y_start; y <= y_end; ++y) {
            const float y_top = std::max(p0.y, static_cast<float>(y));
            const float y_bottom = std::min(p1.y, static_cast<float>(y + 1));
            const float dy = y_bottom - y_top;

            const float x_next = x + dy * dxdy;

            // Use the pixel column for the average X
            const float x_avg = (x + x_next) * 0.5f;
            const int ix = static_cast<int>(std::floor(x_avg));

            if (ix >= 0 && ix < width) {
                const float fx = x_avg - static_cast<float>(ix);

                cells[y * width + ix].area += dy * (1.f - fx) * dir;
                cells[y * width + ix].cover += dy * dir;
            } else if (ix < 0) {
                // If the line is to the left of the image, it's a full fill
                cells[y * width + 0].cover += dy * dir;
            }
            x = x_next;
        }
    }

    [[nodiscard]] std::vector<uint8_t> finalize() const {
        std::vector<uint8_t> pixels(width * height, 0);

        for (int y = 0; y < height; ++y) {
            float accumulation = 0.0f;
            // Flip Y for standard image viewers
            const int dest_y = (height - 1) - y;

            for (int x = 0; x < width; ++x) {
                const auto& cell = cells[y * width + x];

                // Total coverage:
                // accumulation is the fill coming FROM the left.
                // cell.area is the transition within THIS pixel.
                float coverage = accumulation + cell.area;

                // Update for the next pixel
                accumulation += cell.cover;

                // Clamping the absolute value handles the Non-Zero Winding rule
                float alpha = std::clamp(std::abs(coverage), 0.0f, 1.0f);

                pixels[dest_y * width + x] = static_cast<uint8_t>(alpha * 255.0f);
            }
        }
        return pixels;
    }

};
    Bitmap Font::rasterize(const Glyph& glyph) const {
        constexpr int padding = 2; // Increased padding slightly for safety
        const int width = static_cast<int>(std::ceil(glyph.bounds.width())) + padding * 2;
        const int height = static_cast<int>(std::ceil(glyph.bounds.height())) + padding * 2;

        // 1. Flatten the curves into linear segments
        auto segments = flatten(glyph, 0.1f);

        // 2. Initialize the buffer
        Rasterizer buffer{width, height};

        // 3. Offset glyph to fit in the bitmap (padding ensures edges aren't cut)
        const Vec2 offset = {
            -glyph.bounds.min.x + static_cast<float>(padding),
            -glyph.bounds.min.y + static_cast<float>(padding)
        };

        // 4. Fill the cell buffer
        for (const auto& seg : segments) {
            buffer.add_line(
                {seg.p0.x + offset.x, seg.p0.y + offset.y},
                {seg.p1.x + offset.x, seg.p1.y + offset.y}
            );
        }

        // 5. Finalize the pixel data
        Bitmap result;
        result.width = width;
        result.height = height;
        result.pixels = buffer.finalize();

        return result;
    }

}
