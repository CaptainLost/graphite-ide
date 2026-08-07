#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace core
{
    /// A flat `key=value` file with no sections. Loading, changing one key and saving
    /// preserves every other key — writing a single key over the whole file would make
    /// each setting destroy the others.
    class IniFile
    {
    public:
        /// A missing or unreadable file loads as empty rather than failing: an absent
        /// settings file is the normal state on first run.
        static IniFile Load(const std::filesystem::path& path);

        /// Returns an empty string when the key is absent.
        [[nodiscard]] std::string Get(std::string_view key) const;

        void Set(std::string_view key, std::string value);

        [[nodiscard]] bool Save(const std::filesystem::path& path) const;

        [[nodiscard]] std::size_t Count() const noexcept { return m_entries.size(); }

    private:
        struct Entry
        {
            std::string key;
            std::string value;
        };

        std::vector<Entry> m_entries;
    };
}
