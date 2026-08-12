#include "corepch.h"

#include "Core/Fs/AtomicWrite.h"

#include "Core/Fs/FileSystem.h"

#include <fstream>
#include <system_error>

#ifdef CORE_PLATFORM_WINDOWS
    #include <windows.h>
#endif

namespace Core
{
    Result<std::filesystem::path> PickAvailablePath(
        const std::function<std::filesystem::path(int)>& candidateFor)
    {
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            const std::filesystem::path candidate = candidateFor(attempt);

            std::error_code code;
            const bool exists = std::filesystem::exists(candidate, code);

            // A stat error is not "nothing is there" — the same reasoning
            // DocumentWindows::HandleFilesChanged applies before marking an open document
            // missing. Treating it as absence here could hand back a name something already
            // occupies; reporting it is the only answer that cannot be quietly wrong.
            if (code)
            {
                return Error{ErrorCode::IoFailure,
                             std::format("cannot tell whether '{}' exists: {}",
                                         ToUtf8(candidate), code.message())};
            }

            if (!exists)
                return candidate;
        }

        return Error{ErrorCode::IoFailure, "cannot find an unused name after 100 attempts"};
    }

    namespace
    {
        /// The temporary's name, one that nothing on disk currently holds. `path + ".tmp"`
        /// is tried first, then a plain counter appended after it (`.tmp1`, `.tmp2`, ...),
        /// through the same collision check every scratch file beside a target uses.
        Result<std::filesystem::path> PickTemporaryPath(const std::filesystem::path& path)
        {
            return PickAvailablePath(
                [&path](int attempt)
                {
                    std::filesystem::path candidate = path;
                    candidate += attempt == 0 ? std::string{".tmp"}
                                               : std::format(".tmp{}", attempt);
                    return candidate;
                });
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

            // A stat error is not "the target is gone" — the same reasoning
            // DocumentWindows::HandleFilesChanged applies before marking an open document
            // missing. Skipping ReplaceFileW on the strength of a failed stat would silently
            // drop the ACLs, creation time and identity this function exists to keep, with
            // nothing to distinguish that from an ordinary first save. So ReplaceFileW is
            // attempted whenever the target is known to exist OR the stat could not say it
            // doesn't; only a stat that confirmed absence skips straight to MoveFileExW.
            if (targetExists || existsError)
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

                // ReplaceFileW requires an existing target. This code means there genuinely
                // is none — the stat above could not say so, or the file vanished between
                // the two calls — which is the ordinary case MoveFileExW below handles, not
                // a failure to report.
                if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                {
                    std::error_code ignored;
                    std::filesystem::remove(temporary, ignored);

                    return Error{ErrorCodeFor(error),
                                 std::format("cannot replace '{}' (error {})", ToUtf8(path), error)};
                }
            }

            // Nothing to replace: confirmed absent, or ReplaceFileW just said so.
            // MoveFileExW with MOVEFILE_REPLACE_EXISTING also covers the race in which the
            // file appears between whichever check above ran and this call.
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
