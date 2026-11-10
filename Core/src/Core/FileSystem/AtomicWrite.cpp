#include "corepch.h"

#include "Core/FileSystem/AtomicWrite.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/WslFileOperations.h"
#include "Core/Platform/Win32Error.h"

#include <fstream>
#include <system_error>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	namespace {
		constexpr int kAttemptLimit = 100;
	}

	Result<std::filesystem::path> PickAvailablePath(
		const std::function<std::filesystem::path(int)>& candidateFor)
	{
		for (int attempt = 0; attempt < kAttemptLimit; ++attempt)
		{
			const std::filesystem::path candidate = candidateFor(attempt);

			std::error_code statError;
			const bool candidateExists = std::filesystem::exists(candidate, statError);

			// A failed stat is not "nothing is there": taking it for absence would hand back a
			// name something already occupies.
			if (statError)
			{
				return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot tell whether '{}' exists: {}", ToUtf8(candidate), statError.message())};
			}

			if (!candidateExists)
			{
				return candidate;
			}
		}

		return Error{.Code = ErrorCode::IoFailure, .Message = "cannot find an unused name after 100 attempts"};
	}

	namespace {
		std::filesystem::path TemporaryCandidateFor(const std::filesystem::path& target, int attempt)
		{
			std::filesystem::path candidate = target;
			const bool isFirstAttempt = attempt == 0;
			candidate += isFirstAttempt ? std::string{".tmp"} : std::format(".tmp{}", attempt);
			return candidate;
		}

		// Beside the target rather than in the temp directory, so that the temporary is on the
		// same volume and inherits the target directory's ACLs.
		Result<std::filesystem::path> PickTemporaryPath(const std::filesystem::path& target)
		{
			return PickAvailablePath(
				[&target](int attempt) { return TemporaryCandidateFor(target, attempt); });
		}

		Error CannotWriteError(const std::filesystem::path& temporary)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot write '{}'", ToUtf8(temporary))};
		}

		void DiscardTemporary(const std::filesystem::path& temporary)
		{
			std::error_code ignoredError;
			std::filesystem::remove(temporary, ignoredError);
		}

		Result<void> WriteTemporary(const std::filesystem::path& temporary, std::string_view bytes)
		{
			// Scoped so that the stream is closed before the temporary is discarded, because Windows will not delete a file that is still open.
			{
				std::ofstream temporaryFile{temporary, std::ios::binary | std::ios::trunc};

				if (!temporaryFile.is_open())
				{
					return CannotWriteError(temporary);
				}

				temporaryFile.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));

				if (temporaryFile.good())
				{
					return Result<void>::Ok();
				}
			}

			DiscardTemporary(temporary);

			return CannotWriteError(temporary);
		}

		Result<void> SwapIntoPlace(const std::filesystem::path& temporary,
								   const std::filesystem::path& target)
		{
#ifdef CORE_PLATFORM_WINDOWS
			std::error_code statError;
			const bool targetExists = std::filesystem::exists(target, statError);

			// A failed stat counts as a target that may exist, because skipping ReplaceFileW on
			// its strength would silently drop the ACLs, creation time and identity this
			// function exists to keep.
			const bool targetMayExist = targetExists || statError;

			if (targetMayExist)
			{
				// REPLACEFILE_IGNORE_MERGE_ERRORS lets the save go ahead when the target's ACLs or
				// attributes cannot all be carried across, rather than failing it.
				const BOOL replaced = ::ReplaceFileW(target.c_str(), temporary.c_str(), nullptr,
													 REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr);

				if (replaced != 0)
				{
					return Result<void>::Ok();
				}

				const DWORD replaceErrorCode = ::GetLastError();
				const bool targetTurnedOutMissing = replaceErrorCode == ERROR_FILE_NOT_FOUND || replaceErrorCode == ERROR_PATH_NOT_FOUND;

				if (!targetTurnedOutMissing)
				{
					DiscardTemporary(temporary);

					return Error{.Code = ErrorCodeFromWin32(replaceErrorCode), .Message = std::format("cannot replace '{}' (error {})", ToUtf8(target), replaceErrorCode)};
				}
			}

			// MOVEFILE_REPLACE_EXISTING covers a target that appears after the checks that found none.
			const BOOL moved = ::MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);

			if (moved != 0)
			{
				return Result<void>::Ok();
			}

			const DWORD moveErrorCode = ::GetLastError();

			DiscardTemporary(temporary);

			return Error{.Code = ErrorCodeFromWin32(moveErrorCode), .Message = std::format("cannot write '{}' (error {})", ToUtf8(target), moveErrorCode)};
#else
			// The one platform split in Core that must not return Unsupported, because that would
			// make an editor that cannot save. A rename is still atomic, only without the metadata
			// ReplaceFileW keeps.
			//
			// The temporary is a new file, created with the default mode, so the rename would
			// otherwise take a script's executable bit away; the target's own bits go onto it
			// first. A target that does not exist yet has none to keep.
			std::error_code statusError;
			const std::filesystem::file_status targetStatus = std::filesystem::status(target, statusError);

			if (!statusError && std::filesystem::exists(targetStatus))
			{
				std::error_code ignoredError;
				std::filesystem::permissions(temporary, targetStatus.permissions(), ignoredError);
			}

			std::error_code renameError;
			std::filesystem::rename(temporary, target, renameError);

			if (renameError)
			{
				DiscardTemporary(temporary);

				return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot replace '{}': {}", ToUtf8(target), renameError.message())};
			}

			return Result<void>::Ok();
#endif
		}
	}

	Result<void> WriteFileAtomically(const std::filesystem::path& requestedTarget, std::string_view bytes)
	{
		// Win32 cannot replace a file under a WSL share (ReplaceFileW fails there): the distro's
		// agent writes it, keeping its Linux permissions (ADR 0696).
		if (std::optional<Result<WslRoute>> route = RouteThroughWsl(requestedTarget))
		{
			if (!*route)
			{
				return route->GetError();
			}

			return route->GetValue().Operations->WriteFile(route->GetValue().Place, bytes);
		}

		std::filesystem::path target = requestedTarget;

#ifndef CORE_PLATFORM_WINDOWS
		// A rename over a symbolic link replaces the link, leaving the file it pointed at as it
		// was; the write goes to that file instead (compile_commands.json -> build/..., dotfiles).
		std::error_code linkError;

		if (std::filesystem::is_symlink(requestedTarget, linkError))
		{
			const std::filesystem::path resolved = std::filesystem::weakly_canonical(requestedTarget, linkError);

			if (!linkError)
			{
				target = resolved;
			}
		}
#endif

		const Result<std::filesystem::path> temporary = PickTemporaryPath(target);

		if (!temporary)
		{
			return temporary.GetError();
		}

		const Result<void> writeOutcome = WriteTemporary(temporary.GetValue(), bytes);

		if (!writeOutcome)
		{
			return writeOutcome;
		}

		return SwapIntoPlace(temporary.GetValue(), target);
	}
}
