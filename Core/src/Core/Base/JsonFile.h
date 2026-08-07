#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace core
{
    /// A flat `key=value` settings file stored as JSON. Loading, changing one key and
    /// saving preserves every other key — writing a single key over the whole file would
    /// make each setting destroy the others.
    class JsonFile
    {
    public:
        /// A missing or unreadable file loads as empty rather than failing: an absent
        /// settings file is the normal state on first run. A malformed file loads as
        /// empty too, rather than throwing — a hand-edited file must not be able to
        /// crash the IDE.
        static JsonFile Load(const std::filesystem::path& path);

        /// Returns an empty string when the key is absent, or when it holds a
        /// non-string value: a hand-edited file must not be able to crash the IDE.
        [[nodiscard]] std::string Get(std::string_view key) const;

        void Set(std::string_view key, std::string value);

        /// Writes to a `.tmp` beside the target and renames into place, so a failed
        /// write cannot destroy the existing settings, and leaves no debris if the
        /// rename fails.
        [[nodiscard]] bool Save(const std::filesystem::path& path) const;

        [[nodiscard]] std::size_t Count() const noexcept { return m_root.size(); }

    private:
        // Holding the real nlohmann::json type (rather than flattening into our own
        // key/value pairs) is what lets nested settings arrive later without another
        // rewrite of this class.
        nlohmann::json m_root = nlohmann::json::object();
    };
}
