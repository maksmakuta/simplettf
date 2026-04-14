#include "simplettf/simplettf.hpp"

#include <cstring>
#include <fstream>
#include <print>

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
            std::string result(4,' ');
            result[0] = static_cast<char>(tag >> 24 & 0xFF);
            result[1] = static_cast<char>(tag >> 16 & 0xFF);
            result[2] = static_cast<char>(tag >>  8 & 0xFF);
            result[3] = static_cast<char>(tag       & 0xFF);
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

        return font;
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

            std::println("{} | {:8} | {:8}", internal::to_string(info.tag), info.offset, info.size);

            m_tables.push_back(info);
        }
    }

}
