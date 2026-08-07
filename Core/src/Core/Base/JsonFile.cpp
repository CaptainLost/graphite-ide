#include "corepch.h"

#include "Core/Base/JsonFile.h"

#include <fstream>
#include <sstream>
#include <system_error>

namespace core
{
    JsonFile JsonFile::Load(const std::filesystem::path& path)
    {
        JsonFile file;

        std::ifstream stream{path};
        if (!stream.is_open())
            return file;

        std::ostringstream buffer;
        buffer << stream.rdbuf();

        // The non-throwing overload (`allow_exceptions = false`) returns a discarded
        // value on malformed input instead of throwing: nlohmann/json throws by
        // default, and a hand-edited settings file must load as empty rather than
        // crash the IDE, exactly like a missing or unreadable one.
        nlohmann::json parsed = nlohmann::json::parse(buffer.str(), nullptr, false);

        if (parsed.is_discarded() || !parsed.is_object())
            return file;

        file.m_root = std::move(parsed);
        return file;
    }

    std::string JsonFile::Get(std::string_view key) const
    {
        const auto entry = m_root.find(std::string{key});
        if (entry == m_root.end() || !entry->is_string())
            return {};

        return entry->get<std::string>();
    }

    void JsonFile::Set(std::string_view key, std::string value)
    {
        m_root[std::string{key}] = std::move(value);
    }

    bool JsonFile::Save(const std::filesystem::path& path) const
    {
        // Write beside the target and rename into place: truncating the real file first
        // would destroy every stored setting if the write then failed partway.
        std::filesystem::path temporary = path;
        temporary += ".tmp";

        {
            std::ofstream stream{temporary, std::ios::trunc};
            if (!stream.is_open())
                return false;

            // Indented rather than a single-line blob: these files are meant to be
            // opened and edited by hand.
            stream << m_root.dump(4);

            if (!stream.good())
                return false;
        }

        std::error_code code;
        std::filesystem::rename(temporary, path, code);

        if (code)
        {
            // Leave no debris behind when the swap fails; the original file is untouched.
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }

        return true;
    }
}
