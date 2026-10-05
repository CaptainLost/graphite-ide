#pragma once

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <winioctl.h>

#include <filesystem>

// NTFS reserves real clusters for a grown file unless it is sparse. False means the filesystem
// cannot mark it, a fact about the machine rather than the code under test, so a caller skips.
inline bool MarkFileSparse(const std::filesystem::path& path)
{
	constexpr DWORD kNoSharing = 0;
	const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, kNoSharing, nullptr,
										OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	constexpr DWORD kNoBufferBytes = 0;
	DWORD bytesReturned = 0;
	const BOOL succeeded = ::DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, kNoBufferBytes, nullptr,
											 kNoBufferBytes, &bytesReturned, nullptr);

	::CloseHandle(handle);

	return succeeded != FALSE;
}
#endif
