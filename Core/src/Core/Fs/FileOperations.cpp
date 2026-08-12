#include "corepch.h"

#include "Core/Fs/FileOperations.h"

#include "Core/Fs/FileSystem.h"

#include <array>
#include <cctype>
#include <fstream>
#include <string>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <shellapi.h>
#endif

namespace Core {

	namespace {
		bool EqualsIgnoringCase(std::string_view left, std::string_view right)
		{
			if (left.size() != right.size())
				return false;

			for (std::size_t i = 0; i < left.size(); ++i)
			{
				if (std::tolower(static_cast<unsigned char>(left[i])) !=
					std::tolower(static_cast<unsigned char>(right[i])))
				{
					return false;
				}
			}

			return true;
		}
	}

	Result<void> CreateEmptyFile(const std::filesystem::path& path)
	{
#ifdef CORE_PLATFORM_WINDOWS
		// CREATE_NEW makes the OS itself refuse an existing target, so there is no
		// window between a check and the act in which someone else's file could appear
		// and be truncated.
		const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
											FILE_ATTRIBUTE_NORMAL, nullptr);

		if (handle == INVALID_HANDLE_VALUE)
		{
			const DWORD error = ::GetLastError();

			if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
			{
				return Error{ErrorCode::InvalidArgument,
							 std::format("'{}' already exists", ToUtf8(path))};
			}

			if (error == ERROR_ACCESS_DENIED)
			{
				return Error{ErrorCode::AccessDenied,
							 std::format("cannot create '{}': access denied", ToUtf8(path))};
			}

			return Error{ErrorCode::IoFailure,
						 std::format("cannot create '{}' (error {})", ToUtf8(path), error)};
		}

		::CloseHandle(handle);
		return Result<void>::Ok();
#else
		// std::filesystem has no atomic exclusive-create before C++23, and this
		// operation exists precisely to refuse an existing target, so a check-then-act
		// fallback would be a silent downgrade rather than a port.
		return Error{ErrorCode::Unsupported,
					 std::format("creating '{}' is not implemented on this platform",
								 ToUtf8(path))};
#endif
	}

	Result<void> MakeDirectory(const std::filesystem::path& path)
	{
		std::error_code code;
		if (std::filesystem::exists(path, code))
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("'{}' already exists", ToUtf8(path))};
		}

		if (!std::filesystem::create_directory(path, code) || code)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot create '{}': {}", ToUtf8(path), code.message())};
		}

		return Result<void>::Ok();
	}

	Result<void> Rename(const std::filesystem::path& from, const std::filesystem::path& to)
	{
#ifdef CORE_PLATFORM_WINDOWS
		// No MOVEFILE_REPLACE_EXISTING: the OS refuses an occupied destination, so the
		// destination cannot be clobbered by a file that appears after a check.
		if (::MoveFileExW(from.c_str(), to.c_str(), 0) != 0)
			return Result<void>::Ok();

		const DWORD error = ::GetLastError();

		if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
			return Error{ErrorCode::NotFound, std::format("'{}' no longer exists", ToUtf8(from))};

		if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
			return Error{ErrorCode::InvalidArgument, std::format("'{}' already exists", ToUtf8(to))};

		if (error == ERROR_ACCESS_DENIED)
		{
			return Error{ErrorCode::AccessDenied,
						 std::format("cannot rename '{}': access denied", ToUtf8(from))};
		}

		return Error{ErrorCode::IoFailure,
					 std::format("cannot rename '{}' (error {})", ToUtf8(from), error)};
#else
		// std::filesystem::rename replaces an existing destination, which this function
		// must never do; a correct port needs the platform's own non-replacing move.
		return Error{ErrorCode::Unsupported,
					 std::format("renaming '{}' is not implemented on this platform",
								 ToUtf8(from))};
#endif
	}

	Result<std::filesystem::path> Duplicate(const std::filesystem::path& path)
	{
		std::error_code code;
		if (!std::filesystem::exists(path, code))
		{
			return Error{ErrorCode::NotFound, std::format("'{}' no longer exists", ToUtf8(path))};
		}

		const std::filesystem::path target = MakeUniquePath(path);

		std::filesystem::copy(path, target,
							  std::filesystem::copy_options::recursive |
								  std::filesystem::copy_options::copy_symlinks,
							  code);

		if (code)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot copy '{}': {}", ToUtf8(path), code.message())};
		}

		return target;
	}

	Result<void> MoveToRecycleBin(const std::filesystem::path& path)
	{
		std::error_code code;
		if (!std::filesystem::exists(path, code))
		{
			return Error{ErrorCode::NotFound, std::format("'{}' no longer exists", ToUtf8(path))};
		}

#ifdef CORE_PLATFORM_WINDOWS
		// SHFILEOPSTRUCTW::pFrom is a list of paths terminated by an EXTRA null. Passing
		// an ordinary null-terminated string makes the API read past the buffer.
		std::wstring source = path.wstring();
		source.push_back(L'\0');
		source.push_back(L'\0');

		SHFILEOPSTRUCTW operation{};
		operation.wFunc = FO_DELETE;
		operation.pFrom = source.c_str();
		operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;

		const int result = ::SHFileOperationW(&operation);

		if (result != 0 || operation.fAnyOperationsAborted != FALSE)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot move '{}' to the Recycle Bin", ToUtf8(path))};
		}

		return Result<void>::Ok();
#else
		return Error{ErrorCode::Unsupported,
					 std::format("moving '{}' to the recycle bin is not implemented on "
								 "this platform",
								 ToUtf8(path))};
#endif
	}

	std::filesystem::path MakeUniquePath(const std::filesystem::path& path)
	{
		std::error_code code;
		if (!std::filesystem::exists(path, code))
			return path;

		const std::filesystem::path parent = path.parent_path();
		const std::string stem = ToUtf8(path.stem());
		const std::string extension = ToUtf8(path.extension());

		std::filesystem::path candidate = parent / FromUtf8(stem + " copy" + extension);
		int attempt = 2;

		while (std::filesystem::exists(candidate, code))
		{
			candidate = parent / FromUtf8(stem + " copy " + std::to_string(attempt) + extension);
			++attempt;
		}

		return candidate;
	}

	bool IsValidFileName(std::string_view name)
	{
		if (name.empty())
			return false;

		if (name.find_first_not_of(" \t") == std::string_view::npos)
			return false;

		if (name.find_first_of("\\/:*?\"<>|") != std::string_view::npos)
			return false;

		if (name.back() == '.' || name.back() == ' ')
			return false;

		// A reserved device name is reserved with any extension too: NUL.txt cannot be
		// created either. Compare against the part before the first dot.
		const std::size_t dot = name.find('.');
		const std::string_view base = dot == std::string_view::npos ? name : name.substr(0, dot);

		constexpr std::array<std::string_view, 22> kReserved{
			"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
			"COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

		for (const std::string_view reserved : kReserved)
		{
			if (EqualsIgnoringCase(base, reserved))
				return false;
		}

		return true;
	}
}