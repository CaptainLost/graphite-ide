#include "corepch.h"

#include "Core/Base/JsonFile.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace core
{
    struct JsonFile::Document
    {
        nlohmann::json root = nlohmann::json::object();
    };

    JsonFile::JsonFile() : m_document(std::make_unique<Document>())
    {
    }

    JsonFile::~JsonFile() = default;

    JsonFile::JsonFile(JsonFile&&) noexcept = default;
    JsonFile& JsonFile::operator=(JsonFile&&) noexcept = default;

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

        file.m_document->root = std::move(parsed);
        return file;
    }

    std::string JsonFile::Get(std::string_view key) const
    {
        const nlohmann::json& root = m_document->root;

        const auto entry = root.find(std::string{key});
        if (entry == root.end() || !entry->is_string())
            return {};

        return entry->get<std::string>();
    }

    void JsonFile::Set(std::string_view key, std::string value)
    {
        m_document->root[std::string{key}] = std::move(value);
    }

    std::size_t JsonFile::Count() const noexcept
    {
        return m_document->root.size();
    }

    bool JsonFile::Save(const std::filesystem::path& path) const
    {
        // Serialised before the temporary is opened, not into it. dump() is the one call
        // here that can still throw — it rejects invalid UTF-8 unless told otherwise —
        // and throwing with the file already open would leave a .tmp behind, breaking the
        // promise this function makes in its own doc comment.
        //
        // `replace` substitutes U+FFFD for anything malformed instead of refusing. A
        // settings value is not worth an exception crossing out of Core, and a visibly
        // mangled string is a better outcome than a lost file.
        const std::string text =
            m_document->root.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);

        // Write beside the target and rename into place: truncating the real file first
        // would destroy every stored setting if the write then failed partway.
        std::filesystem::path temporary = path;
        temporary += ".tmp";

        {
            std::ofstream stream{temporary, std::ios::trunc};
            if (!stream.is_open())
                return false;

            stream << text;

            if (!stream.good())
            {
                // Half a file is worse than none, and this one is not the target yet.
                stream.close();

                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return false;
            }
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
