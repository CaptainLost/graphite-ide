#include "corepch.h"

#include <cctype>

#include "Core/Fs/FileSystem.h"

#include <system_error>

#ifdef CORE_PLATFORM_WINDOWS
    #include <windows.h>
#endif

namespace Core
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

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string utf8 = path.u8string();
        return std::string{reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }

    std::filesystem::path FromUtf8(std::string_view text)
    {
        const std::u8string utf8{reinterpret_cast<const char8_t*>(text.data()), text.size()};
        return std::filesystem::path{utf8};
    }

    Result<std::vector<DirectoryEntry>> ReadDirectory(const std::filesystem::path& path)
    {
        std::error_code code;
        // Use directory_options::none rather than skip_permission_denied: if the directory
        // itself is unreadable, we must report an error. skip_permission_denied would
        // silently turn that into an empty listing, making unreadable folders indistinguishable
        // from genuinely empty ones, breaking the contract for consumers to mark them inaccessible.
        std::filesystem::directory_iterator iterator{
            path, std::filesystem::directory_options::none, code};

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
            entry.name = ToUtf8(iterator->path().filename());

            std::error_code statusCode;
            entry.isDirectory = iterator->is_directory(statusCode);

            if (statusCode)
            {
                return Error{TranslateError(statusCode),
                             std::format("cannot classify '{}': {}", ToUtf8(entry.path),
                                         statusCode.message())};
            }

            entry.isHidden = IsHidden(entry.path);

            if (!entry.isDirectory)
            {
                std::error_code sizeCode;
                const std::uintmax_t size = iterator->file_size(sizeCode);
                entry.size = sizeCode ? 0 : static_cast<std::uint64_t>(size);
            }

            entries.push_back(std::move(entry));
        }

        return entries;
    }

    int CompareNames(std::string_view left, std::string_view right)
    {
        const std::size_t count = std::min(left.size(), right.size());

        for (std::size_t i = 0; i < count; ++i)
        {
            const int a = std::tolower(static_cast<unsigned char>(left[i]));
            const int b = std::tolower(static_cast<unsigned char>(right[i]));

            if (a != b)
                return a < b ? -1 : 1;
        }

        if (left.size() == right.size())
            return 0;

        return left.size() < right.size() ? -1 : 1;
    }

    std::filesystem::path NormalizeDirectory(const std::filesystem::path& path)
    {
        std::filesystem::path normalized = path.lexically_normal();
        normalized.make_preferred();

        // lexically_normal leaves a trailing separator in place, which shows up as an
        // empty final element and makes the path unequal to the same directory spelled
        // without it. has_filename() is false exactly in that case.
        //
        // has_relative_path() guards the roots: for `D:/` there is nothing to strip, and
        // parent_path() would return `D:` — a drive-relative path meaning "wherever that
        // drive's cursor happens to be", which is not the same directory at all.
        if (!normalized.has_filename() && normalized.has_relative_path())
            normalized = normalized.parent_path();

        return normalized;
    }

    std::optional<std::filesystem::path> RelativeTo(const std::filesystem::path& path,
                                                    const std::filesystem::path& base)
    {
        if (base.empty())
            return std::nullopt;

        const std::filesystem::path relative = path.lexically_relative(base);

        if (relative.empty() || *relative.begin() == "..")
            return std::nullopt;

        return relative;
    }
}
