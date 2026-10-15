#include "corepch.h"

#include "Core/FileSystem/FileOperations.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/UniqueHandle.h"
#include "Core/Platform/Win32Error.h"

#include <fstream>
#include <string>

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

	Result<std::filesystem::path> Duplicate(const std::filesystem::path& path)
	{
		std::error_code error;
		if (!std::filesystem::exists(path, error))
		{
			return NoLongerExistsError(path);
		}

		const std::filesystem::path target = MakeUniquePath(path);

		std::filesystem::copy(path, target,
							  std::filesystem::copy_options::recursive |
								  std::filesystem::copy_options::copy_symlinks,
							  error);

		if (error)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot copy '{}': {}", ToUtf8(path), error.message())};
		}

		return target;
	}

	Result<void> MoveToRecycleBin(const std::filesystem::path& path)
	{
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
		std::error_code ignoredError;
		if (!std::filesystem::exists(path, ignoredError))
		{
			return path;
		}

		const std::filesystem::path parent = path.parent_path();
		const std::string stem = ToUtf8(path.stem());
		const std::string extension = ToUtf8(path.extension());

		std::filesystem::path candidate = parent / FromUtf8(stem + " copy" + extension);
		int copyNumber = kFirstNumberedCopy;

		while (std::filesystem::exists(candidate, ignoredError))
		{
			candidate = parent / FromUtf8(stem + " copy " + std::to_string(copyNumber) + extension);
			++copyNumber;
		}

		return candidate;
	}
}
