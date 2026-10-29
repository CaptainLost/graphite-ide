#pragma once

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <winioctl.h>
#endif

// A file of `fileSize` zeros that takes no room on disk. False means the filesystem cannot mark a file sparse, a fact
// about the machine rather than the code under test, so a caller skips.
inline bool TryWriteSparseFile(const std::filesystem::path& path, std::uint64_t fileSize)
{
	{
		std::ofstream stream{path, std::ios::binary | std::ios::trunc};
	}

#ifdef CORE_PLATFORM_WINDOWS
	// NTFS reserves real clusters for a grown file unless it is sparse.
	constexpr DWORD kNoSharing = 0;
	const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, kNoSharing, nullptr, OPEN_EXISTING,
										FILE_ATTRIBUTE_NORMAL, nullptr);

	if (handle == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	constexpr DWORD kNoBufferBytes = 0;
	DWORD bytesReturned = 0;
	const BOOL marked = ::DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, kNoBufferBytes, nullptr, kNoBufferBytes,
										  &bytesReturned, nullptr);
	::CloseHandle(handle);

	if (marked == FALSE)
	{
		return false;
	}
#endif

	std::error_code resizeError;
	std::filesystem::resize_file(path, fileSize, resizeError);
	REQUIRE_FALSE(resizeError);

	return true;
}
