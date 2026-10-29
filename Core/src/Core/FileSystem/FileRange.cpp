#include "corepch.h"

#include "Core/FileSystem/FileRange.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Win32Error.h"

#include <algorithm>
#include <fstream>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	namespace {
		Error ReadFailedError(const std::filesystem::path& path)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = "Read failed: " + ToUtf8(path)};
		}
	}

	Result<std::vector<std::uint8_t>> ReadFileRange(const std::filesystem::path& path, std::uint64_t offset, std::size_t count)
	{
		std::vector<std::uint8_t> bytes(count);

#ifdef CORE_PLATFORM_WINDOWS
		constexpr DWORD kShareModeAllowingRenameAndDelete = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

		const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, kShareModeAllowingRenameAndDelete, nullptr,
										  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (file == INVALID_HANDLE_VALUE)
		{
			const DWORD openErrorCode = ::GetLastError();

			return Error{.Code = ErrorCodeFromWin32(openErrorCode), .Message = "Cannot open " + ToUtf8(path) + " (error " + std::to_string(openErrorCode) + ")"};
		}

		std::size_t bytesRead = 0;

		while (bytesRead < count)
		{
			constexpr std::size_t kLargestRead = MAXDWORD;
			const DWORD bytesThisCall = static_cast<DWORD>((std::min)(count - bytesRead, kLargestRead));
			const std::uint64_t position = offset + bytesRead;

			OVERLAPPED where{};
			where.Offset = static_cast<DWORD>(position & 0xFFFFFFFFu);
			where.OffsetHigh = static_cast<DWORD>(position >> 32);

			DWORD bytesReadThisCall = 0;
			const BOOL read = ::ReadFile(file, bytes.data() + bytesRead, bytesThisCall, &bytesReadThisCall, &where);

			if (!read)
			{
				const DWORD readErrorCode = ::GetLastError();

				if (readErrorCode == ERROR_HANDLE_EOF)
				{
					break;
				}

				::CloseHandle(file);

				return ReadFailedError(path);
			}

			if (bytesReadThisCall == 0)
			{
				break;
			}

			bytesRead += bytesReadThisCall;
		}

		::CloseHandle(file);

		bytes.resize(bytesRead);
		return bytes;
#else
		std::error_code statError;
		const bool exists = std::filesystem::exists(path, statError);

		if (!exists && !statError)
		{
			return Error{.Code = ErrorCode::NotFound, .Message = "Cannot open " + ToUtf8(path)};
		}

		std::ifstream file(path, std::ios::binary);

		if (!file)
		{
			return Error{.Code = ErrorCode::AccessDenied, .Message = "Cannot open " + ToUtf8(path)};
		}

		file.seekg(static_cast<std::streamoff>(offset));
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));

		if (file.bad())
		{
			return ReadFailedError(path);
		}

		bytes.resize(static_cast<std::size_t>(file.gcount()));
		return bytes;
#endif
	}
}
