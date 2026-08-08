#include "corepch.h"

#include "Core/Sln/SolutionEditor.h"

#include "Core/Base/Uuid.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Sln/SlnxEditor.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace core::sln
{
    namespace
    {
        /// Compared case-insensitively, because the filesystem this runs on is.
        std::string LowercaseExtension(const std::filesystem::path& path)
        {
            std::string extension = fs::ToUtf8(path.extension());

            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char character)
                           { return static_cast<char>(std::tolower(character)); });

            return extension;
        }
    }

    std::string NewGuid()
    {
        // Both solution formats wrap a GUID in braces and write it uppercase. That is the
        // whole of this function: the identifier itself is a plain version 4 UUID, and
        // core::Uuid is where it is made and where the RFC is answered to.
        return std::format("{{{}}}", Uuid::Generate().ToString());
    }

    Result<std::unique_ptr<SolutionEditor>> SolutionEditor::Open(
        const std::filesystem::path& solution)
    {
        std::error_code code;

        if (!std::filesystem::is_regular_file(solution, code))
        {
            return Error{ErrorCode::NotFound,
                         std::format("no solution file at '{}'", solution.string())};
        }

        // .sln is readable but not yet writable; a second plan adds it behind this same
        // call. Refusing here is what keeps a caller from discovering that halfway
        // through an edit.
        if (LowercaseExtension(solution) != ".slnx")
        {
            return Error{ErrorCode::Unsupported,
                         std::format("'{}' is not a format this build can edit",
                                     solution.string())};
        }

        std::ifstream stream{solution, std::ios::binary};

        if (!stream.is_open())
            return Error{ErrorCode::IoFailure, std::format("cannot open '{}'", solution.string())};

        std::ostringstream buffer;
        buffer << stream.rdbuf();

        std::unique_ptr<SolutionEditor> editor =
            std::make_unique<SlnxEditor>(solution, buffer.str());

        return editor;
    }
}
