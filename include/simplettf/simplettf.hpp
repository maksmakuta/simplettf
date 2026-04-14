#ifndef SIMPLETTF_HPP
#define SIMPLETTF_HPP

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace simplettf {

    namespace internal {

        struct TableInfo final {
            std::uint32_t tag;
            std::uint32_t offset;
            std::uint32_t size;
        };

        std::uint32_t as_tag(const std::string& tag);
        std::string to_string(std::uint32_t tag);

    }

    class Font final {
    public:
        static std::expected<Font,std::string> load(const std::filesystem::path& path);

    private:
        void loadTables();

        std::vector<std::byte> m_font_data;
        std::vector<internal::TableInfo> m_tables;
    };

}

#endif //SIMPLETTF_HPP
