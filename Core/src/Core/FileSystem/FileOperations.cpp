#include "corepch.h"

#include "Core/FileSystem/FileOperations.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/WslFileOperations.h"
#include "Core/Platform/UniqueHandle.h"
#include "Core/Platform/Win32Error.h"

#include <cstddef>
#include <format>
#include <fstream>
#include <string>
#include <system_error>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <shellapi.h>
#endif

namespace Core {

	namespace {
		constexpr int kFirstNumberedCopy = 2;

		Error AlreadyExistsError(const std::filesystem::path& path)
		{
			return Error{.Code = ErrorCode::InvalidArgument, .Message = std::format("'{}' already exists", ToUtf8(path))};
		}

		Error NoLongerExistsError(const std::filesystem::path& path)
		{
			return Error{.Code = ErrorCode::NotFound, .Message = std::format("'{}' no longer exists", ToUtf8(path))};
		}

		bool IsTaken(const std::filesystem::path& path, const PathTaken& isReserved)
		{
			std::error_code ignoredError;
			const bool reserved = isReserved && isReserved(path);

			return reserved || std::filesystem::exists(path, ignoredError);
		}

		struct CopyTally
		{
			const CopiedFileCount& OnFileCopied;
			std::size_t CopiedFiles = 0;

			void NoteCopied()
			{
				++CopiedFiles;

				if (OnFileCopied)
				{
					OnFileCopied(CopiedFiles);
				}
			}
		};

		std::error_code CopyEntry(const std::filesystem::path& from, const std::filesystem::path& to, CopyTally& tally);

		std::error_code CopyFolder(const std::filesystem::path& from, const std::filesystem::path& to, CopyTally& tally)
		{
			std::error_code error;
			const bool created = std::filesystem::create_directory(to, from, error);

			if (error)
			{
				return error;
			}

			// WARNING: create_directory answers false with no error for a folder that exists, and copying on would merge.
			if (!created)
			{
				return std::make_error_code(std::errc::file_exists);
			}

			std::filesystem::directory_iterator walk{from, error};

			for (; !error && walk != std::filesystem::directory_iterator{}; walk.increment(error))
			{
				const std::filesystem::path& inside = walk->path();
				const std::error_code copyError = CopyEntry(inside, to / inside.filename(), tally);

				if (copyError)
				{
					return copyError;
				}
			}

			return error;
		}

		std::error_code CopyEntry(const std::filesystem::path& from, const std::filesystem::path& to, CopyTally& tally)
		{
			std::error_code error;
			const std::filesystem::file_status status = std::filesystem::symlink_status(from, error);

			if (error)
			{
				return error;
			}

			if (std::filesystem::is_symlink(status))
			{
				std::filesystem::copy_symlink(from, to, error);
			}
			else if (std::filesystem::is_directory(status))
			{
				return CopyFolder(from, to, tally);
			}
			else if (std::filesystem::is_regular_file(status))
			{
				std::filesystem::copy_file(from, to, std::filesystem::copy_options::none, error);
			}
			else
			{
				return std::make_error_code(std::errc::not_supported);
			}

			if (!error)
			{
				tally.NoteCopied();
			}

			return error;
		}

#ifdef CORE_PLATFORM_WINDOWS
		constexpr DWORD kShareWithNoOne = 0;

		// MoveFileExW without MOVEFILE_REPLACE_EXISTING: the OS itself refuses an occupied destination,
		// which a check before moving could not promise, since a file can appear in between.
		constexpr DWORD kRefuseOccupiedDestination = 0;

		bool IsAlreadyExistsError(DWORD win32Error)
		{
			return win32Error == ERROR_FILE_EXISTS || win32Error == ERROR_ALREADY_EXISTS;
		}
#endif
	}

	Result<void> CreateEmptyFile(const std::filesystem::path& path)
	{
		if (std::optional<Result<WslRoute>> route = RouteThroughWsl(path))
		{
			return *route ? route->GetValue().Operations->CreateEmptyFile(route->GetValue().Place) : route->GetError();
		}

#ifdef CORE_PLATFORM_WINDOWS
		// CREATE_NEW has the OS itself refuse an existing file, which a check before creating could
		// not promise: a file can appear in between and would be truncated.
		const UniqueHandle newFile{::CreateFileW(path.c_str(), GENERIC_WRITE, kShareWithNoOne, nullptr, CREATE_NEW,
												 FILE_ATTRIBUTE_NORMAL, nullptr)};

		if (!newFile)
		{
			const DWORD win32Error = ::GetLastError();

			if (IsAlreadyExistsError(win32Error))
			{
				return AlreadyExistsError(path);
			}

			const ErrorCode errorCode = ErrorCodeFromWin32(win32Error);

			if (errorCode == ErrorCode::AccessDenied)
			{
				return Error{.Code = errorCode, .Message = std::format("cannot create '{}': access denied", ToUtf8(path))};
			}

			return Error{.Code = errorCode, .Message = std::format("cannot create '{}' (error {})", ToUtf8(path), win32Error)};
		}

		return Result<void>::Ok();
#else
		// Before C++23 std::filesystem cannot create a file exclusively, and a check followed by a
		// create races, so a fallback here would be a silent downgrade rather than a port.
		return Error{.Code = ErrorCode::Unsupported, .Message = std::format("creating '{}' is not implemented on this platform", ToUtf8(path))};
#endif
	}

	Result<void> MakeDirectory(const std::filesystem::path& path)
	{
		if (std::optional<Result<WslRoute>> route = RouteThroughWsl(path))
		{
			return *route ? route->GetValue().Operations->MakeDirectory(route->GetValue().Place) : route->GetError();
		}

		std::error_code error;
		if (std::filesystem::exists(path, error))
		{
			return AlreadyExistsError(path);
		}

		const bool created = std::filesystem::create_directory(path, error);

		if (!created || error)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot create '{}': {}", ToUtf8(path), error.message())};
		}

		return Result<void>::Ok();
	}

	Result<void> Rename(const std::filesystem::path& from, const std::filesystem::path& to)
	{
		const std::optional<WslSharePath> fromPlace = ParseWslSharePath(from);
		const std::optional<WslSharePath> toPlace = ParseWslSharePath(to);

		if (fromPlace || toPlace)
		{
			// A move between a distro and Windows, or between two distros, crosses file systems.
			const bool sameDistro = fromPlace && toPlace && fromPlace->Distro == toPlace->Distro;

			if (!sameDistro)
			{
				return Error{ErrorCode::Unsupported, std::format("cannot move '{}' to '{}': they are on different file systems",
																 ToUtf8(from), ToUtf8(to))};
			}

			const std::optional<Result<WslRoute>> route = RouteThroughWsl(from);
			return *route ? route->GetValue().Operations->Rename(*fromPlace, *toPlace) : route->GetError();
		}

#ifdef CORE_PLATFORM_WINDOWS
		const bool moved = ::MoveFileExW(from.c_str(), to.c_str(), kRefuseOccupiedDestination) != 0;

		if (moved)
		{
			return Result<void>::Ok();
		}

		const DWORD win32Error = ::GetLastError();

		if (IsAlreadyExistsError(win32Error))
		{
			return AlreadyExistsError(to);
		}

		const ErrorCode errorCode = ErrorCodeFromWin32(win32Error);

		if (errorCode == ErrorCode::NotFound)
		{
			return NoLongerExistsError(from);
		}

		if (errorCode == ErrorCode::AccessDenied)
		{
			return Error{.Code = errorCode, .Message = std::format("cannot rename '{}': access denied", ToUtf8(from))};
		}

		return Error{.Code = errorCode, .Message = std::format("cannot rename '{}' (error {})", ToUtf8(from), win32Error)};
#else
		// std::filesystem::rename replaces an existing destination, which this function must never
		// do, so a port needs the platform's own move that refuses one.
		return Error{.Code = ErrorCode::Unsupported, .Message = std::format("renaming '{}' is not implemented on this platform", ToUtf8(from))};
#endif
	}

	Result<std::filesystem::path> Duplicate(const std::filesystem::path& path, const CopiedFileCount& onFileCopied)
	{
		return DuplicateTo(path, MakeUniquePath(path), onFileCopied);
	}

	Result<std::filesystem::path> DuplicateTo(const std::filesystem::path& path, const std::filesystem::path& destination,
											  const CopiedFileCount& onFileCopied)
	{
		std::error_code existsError;
		if (!std::filesystem::exists(path, existsError))
		{
			return NoLongerExistsError(path);
		}

		CopyTally tally{.OnFileCopied = onFileCopied};
		const std::error_code error = CopyEntry(path, destination, tally);

		if (error)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot copy '{}': {}", ToUtf8(path), error.message())};
		}

		return destination;
	}

	Result<void> MoveToRecycleBin(const std::filesystem::path& path)
	{
		// Inside WSL the Linux trash takes it, where Linux file managers restore it from.
		if (std::optional<Result<WslRoute>> route = RouteThroughWsl(path))
		{
			return *route ? route->GetValue().Operations->MoveToTrash(route->GetValue().Place) : route->GetError();
		}

		std::error_code ignoredError;
		if (!std::filesystem::exists(path, ignoredError))
		{
			return NoLongerExistsError(path);
		}

#ifdef CORE_PLATFORM_WINDOWS
		// SHFILEOPSTRUCTW::pFrom is a list of paths ended by an extra null, so an ordinary
		// null-terminated string makes the API read past the buffer.
		std::wstring pathList = path.wstring();
		pathList.push_back(L'\0');
		pathList.push_back(L'\0');

		SHFILEOPSTRUCTW operation{
			.wFunc = FO_DELETE,
			.pFrom = pathList.c_str(),
			.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT,
		};

		const int operationStatus = ::SHFileOperationW(&operation);

		if (operationStatus != 0 || operation.fAnyOperationsAborted != FALSE)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot move '{}' to the Recycle Bin", ToUtf8(path))};
		}

		return Result<void>::Ok();
#else
		return Error{.Code = ErrorCode::Unsupported, .Message = std::format("moving '{}' to the recycle bin is not implemented on "
																			"this platform",
																			ToUtf8(path))};
#endif
	}

	std::filesystem::path MakeUniquePath(const std::filesystem::path& path)
	{
		const PathTaken nothingReserved;
		return MakeUniquePath(path, nothingReserved);
	}

	std::filesystem::path MakeUniquePath(const std::filesystem::path& path, const PathTaken& isReserved)
	{
		if (!IsTaken(path, isReserved))
		{
			return path;
		}

		const std::filesystem::path parent = path.parent_path();
		const std::string stem = ToUtf8(path.stem());
		const std::string extension = ToUtf8(path.extension());

		std::filesystem::path candidate = parent / FromUtf8(stem + " copy" + extension);
		int copyNumber = kFirstNumberedCopy;

		while (IsTaken(candidate, isReserved))
		{
			candidate = parent / FromUtf8(stem + " copy " + std::to_string(copyNumber) + extension);
			++copyNumber;
		}

		return candidate;
	}
}
