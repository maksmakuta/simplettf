#ifndef SIMPLETTF_HPP
#define SIMPLETTF_HPP

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace simplettf {

    namespace internal {

        class BufferReader;

        struct TableInfo final {
            std::uint32_t tag;
            std::uint32_t offset;
            std::uint32_t size;
        };

        std::uint32_t as_tag(const std::string& tag);
        std::string to_string(std::uint32_t tag);

    }

    struct Metadata final {
        uint32_t units_per_em{0};
        uint32_t loc_format{0};
        uint32_t glyph_count{0};
    };

    class Font final {
    public:
        static std::expected<Font,std::string> load(const std::filesystem::path& path);

        [[nodiscard]] Metadata getMetadata() const;

    private:
        void loadTables();

        [[nodiscard]] const internal::TableInfo *findTable(const std::string& tag) const;
        [[nodiscard]] std::optional<internal::BufferReader> getReaderFor(const std::string& tag) const;

        Metadata m_metadata;
        std::vector<std::byte> m_font_data;
        std::vector<internal::TableInfo> m_tables;
    };

}

#endif //SIMPLETTF_HPP
