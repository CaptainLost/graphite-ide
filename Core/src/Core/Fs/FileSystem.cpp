#include "corepch.h"

#include "Core/Fs/FileSystem.h"

#include <system_error>

#ifdef CORE_PLATFORM_WINDOWS
    #include <windows.h>
#endif

namespace core::fs
{
    namespace
    {
        ErrorCode TranslateError(const std::error_code& code)
        {
            if (code == std::errc::no_such_file_or_directory)
                return ErrorCode::NotFound;

            if (code == std::errc::permission_denied)
                return ErrorCode::AccessDenied;

            if (code == std::errc::not_a_directory)
                return ErrorCode::InvalidArgument;

            return ErrorCode::IoFailure;
        }

        bool IsHidden(const std::filesystem::path& path)
        {
#ifdef CORE_PLATFORM_WINDOWS
            const DWORD attributes = ::GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
                return false;

            return (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
#else
            const std::string name = path.filename().string();
            return !name.empty() && name.front() == '.';
#endif
        }
    }

    Result<std::vector<DirectoryEntry>> ReadDirectory(const std::filesystem::path& path)
    {
        std::error_code code;
        std::filesystem::directory_iterator iterator{
            path, std::filesystem::directory_options::skip_permission_denied, code};

        if (code)
        {
            return Error{TranslateError(code),
                         std::format("cannot read directory '{}': {}", path.string(), code.message())};
        }

        std::vector<DirectoryEntry> entries;

        for (; iterator != std::filesystem::directory_iterator{}; iterator.increment(code))
        {
            if (code)
            {
                return Error{TranslateError(code),
                             std::format("cannot enumerate '{}': {}", path.string(), code.message())};
            }

            DirectoryEntry entry;
            entry.path = iterator->path();
            entry.name = iterator->path().filename().string();

            std::error_code statusCode;
            entry.isDirectory = iterator->is_directory(statusCode);
            entry.isHidden = IsHidden(entry.path);

            if (!entry.isDirectory)
            {
                const std::uintmax_t size = iterator->file_size(statusCode);
                entry.size = statusCode ? 0 : static_cast<std::uint64_t>(size);
            }

            entries.push_back(std::move(entry));
        }

        return entries;
    }
}
