#include "corepch.h"

#include "Core/Fs/AtomicWrite.h"

#include "Core/Fs/FileSystem.h"

#include <fstream>
#include <system_error>

#ifdef CORE_PLATFORM_WINDOWS
    #include <windows.h>
#endif

namespace core::fs
{
    namespace
    {
        /// The temporary's name, one that nothing on disk currently holds. `path + ".tmp"`
        /// is tried first, then a counter is appended, because two saves racing — or a
        /// stray file left from an earlier crash — must not make one clobber the other's
        /// scratch file.
        Result<std::filesystem::path> PickTemporaryPath(const std::filesystem::path& path)
        {
            std::filesystem::path candidate = path;
            candidate += ".tmp";

            std::error_code code;

            for (int attempt = 0; std::filesystem::exists(candidate, code); ++attempt)
            {
                if (attempt >= 100)
                {
                    return Error{ErrorCode::IoFailure,
                                 std::format("cannot find an unused temporary name beside '{}'",
                                             ToUtf8(path))};
                }

                candidate = path;
                candidate += std::format(".tmp{}", attempt + 1);
            }

            return candidate;
        }

        /// Writes `bytes` to `temporary`, leaving nothing behind on failure. The target
        /// itself is never touched by this step.
        Result<void> WriteTemporary(const std::filesystem::path& temporary, std::string_view bytes)
        {
            {
                std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};

                if (!stream.is_open())
                {
                    return Error{ErrorCode::IoFailure,
                                 std::format("cannot write '{}'", ToUtf8(temporary))};
                }

                stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));

                if (stream.good())
                    return Result<void>::Ok();
            }

            // Half a file is worse than none, and this one is not the target yet.
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);

            return Error{ErrorCode::IoFailure, std::format("cannot write '{}'", ToUtf8(temporary))};
        }

#ifdef CORE_PLATFORM_WINDOWS
        /// A read-only attribute and a file held open by another program are the two most
        /// common ways a save fails, and both arrive here. Distinguishing them from a
        /// disk problem is what lets the message name something the user can act on.
        ///
        /// The switch carries a `default`, unlike the ones over `enum class` elsewhere in
        /// this codebase: DWORD is not an enumeration and there is no closed set of
        /// Windows error codes to be exhaustive about.
        ErrorCode ErrorCodeFor(DWORD error)
        {
            switch (error)
            {
            case ERROR_ACCESS_DENIED:
            case ERROR_SHARING_VIOLATION:
                return ErrorCode::AccessDenied;
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                return ErrorCode::NotFound;
            default:
                return ErrorCode::IoFailure;
            }
        }
#endif

        /// Swaps `temporary` into `path`, replacing whatever is there. On success the
        /// temporary no longer exists — it has become `path`, or its content has.
        Result<void> SwapIntoPlace(const std::filesystem::path& temporary,
                                    const std::filesystem::path& path)
        {
#ifdef CORE_PLATFORM_WINDOWS
            std::error_code existsError;
            const bool targetExists = std::filesystem::exists(path, existsError);

            if (targetExists)
            {
                // REPLACEFILE_IGNORE_MERGE_ERRORS: proceed when the target's ACLs or
                // attributes cannot all be carried across, rather than failing the save.
                // The content is what the user asked for; the metadata is what we prefer.
                if (::ReplaceFileW(path.c_str(), temporary.c_str(), nullptr,
                                    REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != 0)
                {
                    return Result<void>::Ok();
                }

                const DWORD error = ::GetLastError();

                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);

                return Error{ErrorCodeFor(error),
                             std::format("cannot replace '{}' (error {})", ToUtf8(path), error)};
            }

            // Nothing to replace. MoveFileExW with MOVEFILE_REPLACE_EXISTING covers the
            // race in which the file appears between the check above and this call.
            if (::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0)
                return Result<void>::Ok();

            const DWORD error = ::GetLastError();

            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);

            return Error{ErrorCodeFor(error),
                         std::format("cannot write '{}' (error {})", ToUtf8(path), error)};
#else
            // Not every platform exposes an equivalent of ReplaceFileW, so this branch
            // deliberately does NOT return ErrorCode::Unsupported the way every other
            // platform split in Core does: that would mean an editor that cannot save.
            // A rename is still atomic, just without the metadata preservation the
            // Windows branch above gets from ReplaceFileW.
            std::error_code code;
            std::filesystem::rename(temporary, path, code);

            if (code)
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);

                return Error{ErrorCode::IoFailure,
                             std::format("cannot replace '{}': {}", ToUtf8(path), code.message())};
            }

            return Result<void>::Ok();
#endif
        }
    }

    Result<void> WriteFileAtomically(const std::filesystem::path& path, std::string_view bytes)
    {
        const Result<std::filesystem::path> temporary = PickTemporaryPath(path);

        if (!temporary)
            return temporary.GetError();

        const Result<void> written = WriteTemporary(temporary.Value(), bytes);

        if (!written)
            return written;

        return SwapIntoPlace(temporary.Value(), path);
    }
}
