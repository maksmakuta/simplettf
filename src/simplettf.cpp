#include "simplettf/simplettf.hpp"

#include <algorithm>
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

    GlyphID Font::getGlyphID(const uint32_t codepoint) const {
        std::println("Segment count: {}", m_segments.size());
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
            return (codepoint + it->id_delta) & 0xFFFF;
        }

        const internal::BufferReader reader(m_font_data);
        const uint32_t glyph_offset = it->offset_in_file + it->id_range_offset + (codepoint - it->start_code) * 2;

        if (const auto glyph_id = reader.peek<uint16_t>(glyph_offset); glyph_id != 0) {
            return glyph_id + it->id_delta & 0xFFFF;
        }

        return 0;
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
    }

}
