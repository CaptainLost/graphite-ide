#include "corepch.h"

#include "Core/FileSystem/AtomicWrite.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Win32Error.h"

#include <fstream>
#include <system_error>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	Result<std::filesystem::path> PickAvailablePath(
		const std::function<std::filesystem::path(int)>& candidateFor)
	{
		for (int attempt = 0; attempt < 100; ++attempt)
		{
			const std::filesystem::path candidate = candidateFor(attempt);

			std::error_code code;
			const bool exists = std::filesystem::exists(candidate, code);

			// A stat error is not "nothing is there": treating it as absence would hand back
			// a name something already occupies.
			if (code)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot tell whether '{}' exists: {}",
										 ToUtf8(candidate), code.message())};
			}

			if (!exists)
			{
				return candidate;
			}
		}

		return Error{ErrorCode::IoFailure, "cannot find an unused name after 100 attempts"};
	}

	namespace {
		Result<std::filesystem::path> PickTemporaryPath(const std::filesystem::path& path)
		{
			return PickAvailablePath(
				[&path](int attempt) {
					std::filesystem::path candidate = path;
					candidate += attempt == 0 ? std::string{".tmp"}
											  : std::format(".tmp{}", attempt);
					return candidate;
				});
		}

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
				{
					return Result<void>::Ok();
				}
			}

			// Half a file is worse than none, and this one is not the target yet.
			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);

			return Error{ErrorCode::IoFailure, std::format("cannot write '{}'", ToUtf8(temporary))};
		}

		Result<void> SwapIntoPlace(const std::filesystem::path& temporary,
								   const std::filesystem::path& path)
		{
#ifdef CORE_PLATFORM_WINDOWS
			std::error_code existsError;
			const bool targetExists = std::filesystem::exists(path, existsError);

			// A stat error is not "the target is gone": skipping ReplaceFileW on the strength
			// of a failed stat would silently drop the ACLs, creation time and identity this
			// function exists to keep, so only a stat confirming absence goes straight to
			// MoveFileExW.
			if (targetExists || existsError)
			{
				// REPLACEFILE_IGNORE_MERGE_ERRORS proceeds when the target's ACLs or
				// attributes cannot all be carried across, rather than failing the save.
				if (::ReplaceFileW(path.c_str(), temporary.c_str(), nullptr,
								   REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != 0)
				{
					return Result<void>::Ok();
				}

				const DWORD error = ::GetLastError();

				// ReplaceFileW requires an existing target, so this code means there is
				// genuinely none: the ordinary case MoveFileExW below handles.
				if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
				{
					std::error_code ignored;
					std::filesystem::remove(temporary, ignored);

					return Error{ErrorCodeFromWin32(error),
								 std::format("cannot replace '{}' (error {})", ToUtf8(path), error)};
				}
			}

			// MOVEFILE_REPLACE_EXISTING covers the race in which the file appears between
			// whichever check above ran and this call.
			if (::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0)
			{
				return Result<void>::Ok();
			}

			const DWORD error = ::GetLastError();

			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);

			return Error{ErrorCodeFromWin32(error),
						 std::format("cannot write '{}' (error {})", ToUtf8(path), error)};
#else
			// The one platform split in Core that must not return Unsupported — that would
			// mean an editor that cannot save. A rename is still atomic, just without the
			// metadata ReplaceFileW preserves above.
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
		{
			return temporary.GetError();
		}

		const Result<void> written = WriteTemporary(temporary.GetValue(), bytes);

		if (!written)
		{
			return written;
		}

		return SwapIntoPlace(temporary.GetValue(), path);
	}
}