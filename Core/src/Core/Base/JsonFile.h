#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace core
{
    /// A settings file: string values under string keys, stored as JSON. Loading,
    /// changing one key and saving preserves every other key — writing a single key over
    /// the whole file would make each setting destroy the others.
    ///
    /// JSON rather than `key=value` because settings stop being flat quickly — lists of
    /// open files, of expanded folders — and every list in a flat format is a private
    /// convention someone has to invent and defend. It also escapes: a value holding a
    /// newline round-trips instead of quietly truncating the file.
    class JsonFile
    {
    public:
        JsonFile();
        ~JsonFile();

        JsonFile(JsonFile&&) noexcept;
        JsonFile& operator=(JsonFile&&) noexcept;

        JsonFile(const JsonFile&) = delete;
        JsonFile& operator=(const JsonFile&) = delete;

        /// A missing or unreadable file loads as empty rather than failing: an absent
        /// settings file is the normal state on first run. A malformed file loads as
        /// empty too, rather than throwing — a hand-edited file must not be able to
        /// crash the IDE.
        [[nodiscard]] static JsonFile Load(const std::filesystem::path& path);

        /// Returns an empty string when the key is absent, or when it holds a
        /// non-string value: a hand-edited file must not be able to crash the IDE.
        [[nodiscard]] std::string Get(std::string_view key) const;

        void Set(std::string_view key, std::string value);

        /// Writes to a `.tmp` beside the target and renames into place, so a failed
        /// write cannot destroy the existing settings, and leaves no debris if the
        /// rename fails.
        [[nodiscard]] bool Save(const std::filesystem::path& path) const;

        [[nodiscard]] std::size_t Count() const noexcept;

    private:
        /// The JSON document lives behind a pointer so that <nlohmann/json.hpp> — some
        /// twenty-five thousand lines — stays inside one translation unit instead of
        /// reaching everything that wants to read a setting. Without this, `IDE` needs a
        /// build-system entry for a serialisation library it never names, and the cost
        /// grows with every consumer.
        ///
        /// An allocation per settings file is not a cost worth weighing: these are read
        /// when a menu is clicked.
        struct Document;
        std::unique_ptr<Document> m_document;
    };
}
