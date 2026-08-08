#include "corepch.h"

#include "Core/Sln/SolutionEditor.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Sln/SlnxEditor.h"

#ifdef CORE_PLATFORM_WINDOWS
    #include <windows.h>

    // WIN32_LEAN_AND_MEAN keeps <windows.h> from reaching COM, and CoCreateGuid lives
    // there.
    #include <objbase.h>
#endif

#include <cctype>
#include <cstdio>
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
#ifdef CORE_PLATFORM_WINDOWS
        GUID guid{};

        if (FAILED(::CoCreateGuid(&guid)))
            return {};

        // The braced uppercase form both formats use. Assembled by hand rather than with
        // StringFromGUID2 to avoid a COM allocation and a wide-to-narrow conversion for
        // thirty-eight ASCII characters.
        char buffer[39] = {};

        std::snprintf(buffer, sizeof(buffer),
                      "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                      static_cast<unsigned long>(guid.Data1),
                      static_cast<unsigned int>(guid.Data2),
                      static_cast<unsigned int>(guid.Data3),
                      static_cast<unsigned int>(guid.Data4[0]),
                      static_cast<unsigned int>(guid.Data4[1]),
                      static_cast<unsigned int>(guid.Data4[2]),
                      static_cast<unsigned int>(guid.Data4[3]),
                      static_cast<unsigned int>(guid.Data4[4]),
                      static_cast<unsigned int>(guid.Data4[5]),
                      static_cast<unsigned int>(guid.Data4[6]),
                      static_cast<unsigned int>(guid.Data4[7]));

        return std::string{buffer};
#else
        return {};
#endif
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
