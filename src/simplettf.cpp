#include "simplettf/simplettf.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numeric>
#include <execution>
#include <span>

namespace simplettf {

    namespace internal {

        class BufferReader final {
        public:
            explicit BufferReader(const std::span<const std::byte> data) : m_data(data) {}

            template<typename T>
                requires std::is_arithmetic_v<T>
            T read() {
                T value = peek<T>(m_offset);
                m_offset += sizeof(T);
                return value;
            }

            template<typename T>
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

            void seek(const size_t position) {
                if (position > m_data.size()) {
                    throw std::runtime_error("BufferReader: Seek out of bounds");
                }
                m_offset = position;
            }

            void skip(const size_t bytes) { seek(m_offset + bytes); }

            [[nodiscard]] size_t tell() const { return m_offset; }

        private:
            std::span<const std::byte> m_data;
            size_t m_offset = 0;
        };

        std::uint32_t as_tag(const std::string &tag) {
            if (tag.size() != 4) {
                return 0;
            }
            return static_cast<std::uint32_t>(static_cast<unsigned char>(tag[0])) << 24 |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(tag[1])) << 16 |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(tag[2])) << 8 |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(tag[3]));
        }

        std::string to_string(const std::uint32_t tag) {
            std::string result;
            result.resize_and_overwrite(4, [tag](char *buf, size_t) {
                buf[0] = static_cast<char>(tag >> 24 & 0xFF);
                buf[1] = static_cast<char>(tag >> 16 & 0xFF);
                buf[2] = static_cast<char>(tag >> 8 & 0xFF);
                buf[3] = static_cast<char>(tag & 0xFF);
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

    std::expected<Font, std::string> Font::load(const std::filesystem::path &path) {
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

        if (!ifs.read(reinterpret_cast<char *>(font.m_font_data.data()), static_cast<std::streamsize>(fileSize))) {
            return std::unexpected{"Failed to read font data"};
        }

        font.loadTables();
        font.populateGlyphCache();

        return font;
    }

    const internal::TableInfo *Font::findTable(const std::string &tag) const {
        const auto t = internal::as_tag(tag);
        const auto it = std::ranges::find_if(m_tables, [&](const auto &info) {
            return info.tag == t;
        });
        return it != m_tables.end() ? &*it : nullptr;
    }

    std::optional<internal::BufferReader> Font::getReaderFor(const std::string &tag) const {
        const auto *table = findTable(tag);
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
        auto &reader = *reader_opt;

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

    void Font::parseFormat12(internal::BufferReader &reader) {
        reader.skip(10); // format, reserved, length, language
        const auto numGroups = reader.read<uint32_t>();
        m_segments.reserve(numGroups);

        for (uint32_t i = 0; i < numGroups; ++i) {
            m_segments.push_back({
                .start_code = reader.read<uint32_t>(),
                .end_code = reader.read<uint32_t>(),
                .start_glyph_id = reader.read<uint32_t>(),
                .id_range_offset = 0xFFFF // Sentinel for Format 12
            });
        }
    }

    void Font::parseFormat4(internal::BufferReader &reader) {
        // format (2), length (2), language (2) = 6 bytes
        reader.skip(6);

        const uint16_t segCount = reader.read<uint16_t>() / 2;
        reader.skip(6); // searchRange, entrySelector, rangeShift

        const size_t endCodeOffset = reader.tell();
        const size_t startCodeOffset = endCodeOffset + segCount * 2 + 2; // +2 for reservedPad
        const size_t idDeltaOffset = startCodeOffset + segCount * 2;
        const size_t idRangeOffsetBase = idDeltaOffset + segCount * 2;

        m_segments.reserve(m_segments.size() + segCount);

        for (uint16_t i = 0; i < segCount; ++i) {
            const auto end = reader.peek<uint16_t>(endCodeOffset + i * 2);
            const auto start = reader.peek<uint16_t>(startCodeOffset + i * 2);
            const auto delta = reader.peek<int16_t>(idDeltaOffset + i * 2);

            const auto currentOffsetInFile = static_cast<uint32_t>(idRangeOffsetBase + i * 2);
            const auto rangeOffset = reader.peek<uint16_t>(currentOffsetInFile);

            m_segments.push_back({
                .start_code = start,
                .end_code = end,
                .start_glyph_id = 0,
                .id_delta = delta,
                .id_range_offset = rangeOffset,
                .offset_in_file = currentOffsetInFile
            });
        }

        reader.seek(idRangeOffsetBase + segCount * 2);
    }

    void Font::getSimpleGlyph(
        internal::BufferReader &reader,
        Glyph &glyph,
        const short num_contours,
        const float scale
    ) {
        if (num_contours == 0) return;

        glyph.contour_indices.resize(static_cast<std::size_t>(num_contours));
        for (auto &end: glyph.contour_indices) {
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

    std::expected<Glyph, std::string> Font::getGlyph(const GlyphID glyphID, const float size) const {
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

        auto &reader = *glyf_opt;
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

    void Font::getGlyphMetrics(const GlyphID gid, Glyph &glyph, const float scale) const {
        if (gid >= m_metadata.glyph_count) {
            glyph.advance = 0.0f;
            glyph.lsb = 0.0f;
            return;
        }

        auto reader_opt = getReaderFor("hmtx");
        if (!reader_opt) return;
        auto &reader = *reader_opt;

        uint16_t raw_advance = 0;
        int16_t raw_lsb = 0;

        if (gid < m_metadata.metrics_count) {
            // Simple case: glyph has both unique advance and unique LSB
            // Entry size is 4 bytes (2 advance + 2 lsb)
            reader.seek(gid * 4);
            raw_advance = reader.read<uint16_t>();
            raw_lsb = reader.read<int16_t>();
        } else {
            reader.seek((m_metadata.metrics_count - 1) * 4);
            raw_advance = reader.read<uint16_t>();
            const uint32_t lsb_offset = m_metadata.metrics_count * 4 + (gid - m_metadata.metrics_count) * 2;
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
        m_tables.reserve(numTables);
        reader.skip(6);

        for (int i = 0; i < numTables; ++i) {
            internal::TableInfo info{};
            info.tag = reader.read<uint32_t>();
            reader.skip(4);
            info.offset = reader.read<uint32_t>();
            info.size = reader.read<uint32_t>();
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

        auto &reader = *reader_opt;
        uint32_t start = 0;
        uint32_t end = 0;

        if (m_metadata.loc_format == 0) {
            start = static_cast<uint32_t>(reader.peek<uint16_t>(id * 2)) * 2;
            end = static_cast<uint32_t>(reader.peek<uint16_t>((id + 1) * 2)) * 2;
        } else {
            start = reader.peek<uint32_t>(id * 4);
            end = reader.peek<uint32_t>((id + 1) * 4);
        }

        return {.offset = start, .length = end - start};
    }

    float Font::getLineHeight(const float fontSize) const {
        const float scale = fontSize / static_cast<float>(m_metadata.units_per_em);
        return static_cast<float>(m_metadata.ascent - m_metadata.descent + m_metadata.line_gap) * scale;
    }

    struct Segment {
        [[maybe_unused]] Vec2 p0, p1;
    };

    struct Cell {
        int32_t cover{0};
        int32_t area{0};
    };

    void flatten_quadratic(const Vec2 p0, const Vec2 p1, const Vec2 p2, std::vector<Segment> &out, const float tol) {
        const float dx = (p0.x + p2.x) * 0.5f - p1.x;
        if (const float dy = (p0.y + p2.y) * 0.5f - p1.y; dx * dx + dy * dy > tol) {
            const Vec2 p01 = {(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
            const Vec2 p12 = {(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
            const Vec2 mid = {(p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f};

            flatten_quadratic(p0, p01, mid, out, tol);
            flatten_quadratic(mid, p12, p2, out, tol);
        } else {
            out.push_back({p0, p2});
        }
    }

    std::vector<Segment> flatten(const Glyph &glyph, const float tolerance) {
        std::vector<Segment> segments;
        segments.reserve(glyph.points.size());

        for (size_t i = 0; i < glyph.contour_indices.size(); ++i) {
            auto contour = glyph.getContour(i);
            if (contour.empty()) continue;

            const Vec2 start_point = contour[0].position;
            Vec2 current = start_point;

            for (size_t j = 1; j < contour.size(); ++j) {
                if (contour[j].on_curve) {
                    segments.push_back({current, contour[j].position});
                    current = contour[j].position;
                } else {
                    const Vec2 p1 = contour[j].position;
                    Vec2 p2;

                    if (const size_t next_idx = (j + 1) % contour.size(); contour[next_idx].on_curve) {
                        p2 = contour[next_idx].position;
                        j++;
                    } else {
                        p2 = {
                            (p1.x + contour[next_idx].position.x) * 0.5f,
                            (p1.y + contour[next_idx].position.y) * 0.5f
                        };
                    }

                    flatten_quadratic(current, p1, p2, segments, tolerance);
                    current = p2;
                }
            }

            if (current.x != start_point.x || current.y != start_point.y) {
                segments.push_back({current, start_point});
            }
        }
        return segments;
    }

    struct Rasterizer {
        int width, height;
        std::vector<Cell> cells;

        Rasterizer(const int w, const int h) : width(w), height(h), cells(w * h, {0, 0}) {}

        void add_line(const Vec2 p0, const Vec2 p1) {
            constexpr int32_t SC = 65536;
            auto x0 = static_cast<int32_t>(p0.x * SC);
            auto y0 = static_cast<int32_t>(p0.y * SC);
            auto x1 = static_cast<int32_t>(p1.x * SC);
            auto y1 = static_cast<int32_t>(p1.y * SC);

            if (y0 == y1) return;

            const int32_t dir = y1 > y0 ? 1 : -1;
            if (y0 > y1) {
                std::swap(x0, x1);
                std::swap(y0, y1);
            }

            const int64_t dxdy = (static_cast<int64_t>(x1 - x0) << 16) / (y1 - y0);
            int32_t x_curr = x0;

            const int y_start = y0 >> 16;
            const int y_end = y1 >> 16;

            Cell *row_ptr = &cells[0];

            for (int y = y_start; y <= y_end; ++y) {
                if (y < height) {
                    const int32_t y_scan = y << 16;
                    const int32_t y_top = y0 > y_scan ? y0 : y_scan;
                    const int32_t y_bot = y1 < y_scan + SC ? y1 : y_scan + SC;
                    const int32_t dy = y_bot - y_top;

                    const int32_t x_next = x_curr + static_cast<int32_t>((static_cast<int64_t>(dy) * dxdy) >> 16);
                    const int32_t x_avg = (x_curr + x_next) >> 1;
                    const int ix = x_avg >> 16;

                    Cell *current_row = row_ptr + y * width;

                    if (ix < width) {
                        const int32_t fx = x_avg & 0xFFFF;
                        const auto area_inc = static_cast<int32_t>((static_cast<int64_t>(dy) * (SC - fx)) >> 16);

                        current_row[ix].area += area_inc * dir;
                        current_row[ix].cover += dy * dir;
                    } else if (ix < 0) {
                        current_row[0].cover += dy * dir;
                    }
                    x_curr = x_next;
                }
            }
        }

        [[nodiscard]] std::vector<uint8_t> finalize() const {
            std::vector<uint8_t> pixels(width * height);
            for (int y = 0; y < height; ++y) {
                int32_t accumulation = 0;
                const Cell *row_start = &cells[y * width];
                uint8_t *dest_row = &pixels[(height - 1 - y) * width];

                for (int x = 0; x < width; ++x) {
                    const auto &[cover, area] = row_start[x];
                    int32_t coverage = accumulation + area;
                    accumulation += cover;
                    coverage = std::abs(coverage);
                    if (coverage > 65536) coverage = 65536;
                    dest_row[x] = static_cast<uint8_t>((coverage * 255) >> 16);
                }
            }
            return pixels;
        }
    };

    Bitmap Font::rasterize(const Glyph &glyph) {
        constexpr int padding = 2; // Increased padding slightly for safety
        const int width = static_cast<int>(std::ceil(glyph.bounds.width())) + padding * 2;
        const int height = static_cast<int>(std::ceil(glyph.bounds.height())) + padding * 2;
        const auto segments = flatten(glyph, 0.1f);
        Rasterizer buffer{width, height};
        const Vec2 offset = {
            -glyph.bounds.min.x + static_cast<float>(padding),
            -glyph.bounds.min.y + static_cast<float>(padding)
        };
        for (const auto &[p0, p1]: segments) {
            buffer.add_line(
                {p0.x + offset.x, p0.y + offset.y},
                {p1.x + offset.x, p1.y + offset.y}
            );
        }

        return Bitmap{static_cast<uint32_t>(width), static_cast<uint32_t>(height), buffer.finalize()};
    }

#include <execution>

    Bitmap Font::rasterizeSDF(const Glyph& glyph, const float spread) {
        const float spread_sq = spread * spread;
        const int padding = static_cast<int>(spread);

        const int width = static_cast<int>(std::ceil(glyph.bounds.width())) + padding * 2;
        const int height = static_cast<int>(std::ceil(glyph.bounds.height())) + padding * 2;

        const auto segments = flatten(glyph, 0.25f);

        struct PreparedSegment {
            Vec2 p0, p1, ab;
            float inv_len_sq;
            float min_y, max_y, min_x, max_x;
        };

        std::vector<PreparedSegment> prepared;
        prepared.reserve(segments.size());
        for (const auto&[p0, p1] : segments) {
            const float dx = p1.x - p0.x;
            const float dy = p1.y - p0.y;
            prepared.push_back({
                p0, p1, {dx, dy},
                dx*dx + dy*dy > 0.0f ? 1.0f / (dx*dx + dy*dy) : 0.0f,
                std::min(p0.y, p1.y), std::max(p0.y, p1.y),
                std::min(p0.x, p1.x), std::max(p0.x, p1.x)
            });
        }

        Bitmap result{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        result.pixels.resize(width * height);

        std::vector<int> rows(height);
        std::iota(rows.begin(), rows.end(), 0);

        std::for_each(std::execution::par_unseq, rows.begin(), rows.end(), [&](const int y) {
            const float py = glyph.bounds.max.y + static_cast<float>(padding) - static_cast<float>(y);
            const uint32_t row_start = y * width;

            std::vector<const PreparedSegment*> active_segments;
            active_segments.reserve(prepared.size() / 4);
            for (const auto& s : prepared) {
                if (py >= s.min_y && py < s.max_y) active_segments.push_back(&s);
            }

            for (int x = 0; x < width; ++x) {
                const float px = glyph.bounds.min.x - static_cast<float>(padding) + static_cast<float>(x);

                float min_d_sq = spread_sq;
                int intersections = 0;

                for (const auto* s : active_segments) {
                    if (const float t_ray = (py - s->p0.y) / s->ab.y; px < s->p0.x + t_ray * s->ab.x)
                        intersections++;
                }

                for (const auto& s : prepared) {
                    const float dy_box = py < s.min_y ? s.min_y - py : py > s.max_y ? py - s.max_y : 0.0f;
                    if (dy_box * dy_box >= min_d_sq) continue;

                    if (const float dx_box = px < s.min_x ? s.min_x - px : px > s.max_x ? px - s.max_x : 0.0f; dx_box * dx_box + dy_box * dy_box >= min_d_sq)
                        continue;

                    const float apx = px - s.p0.x;
                    const float apy = py - s.p0.y;
                    float t = (apx * s.ab.x + apy * s.ab.y) * s.inv_len_sq;
                    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;

                    const float dx = px - (s.p0.x + t * s.ab.x);
                    const float dy = py - (s.p0.y + t * s.ab.y);

                    if (const float d_sq = dx * dx + dy * dy; d_sq < min_d_sq)
                        min_d_sq = d_sq;
                }

                float dist = std::sqrt(min_d_sq);
                if (intersections % 2 == 0) dist = -dist;

                float alpha = 128.0f + dist / spread * 128.0f;
                result.pixels[row_start + x] = static_cast<uint8_t>(std::clamp(alpha, 0.0f, 255.0f));
            }
        });

        return result;
    }

}
