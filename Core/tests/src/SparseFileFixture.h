#pragma once

#include <doctest/doctest.h>

#include "Core/Text/FileProbe.h"

#include "FileFixtures.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <winioctl.h>

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

// A NUL in the sample is what makes the probe call a file Binary, whatever its size.
inline bool TryWriteLargeBinaryFile(const std::filesystem::path& path, std::uint64_t fileSize)
{
	WriteFileBytes(path, std::string(Core::ProbeSampleSize, '\0'));

#ifdef CORE_PLATFORM_WINDOWS
	if (!MarkFileSparse(path))
	{
		return false;
	}
#endif

	std::error_code resizeError;
	std::filesystem::resize_file(path, fileSize, resizeError);
	REQUIRE_FALSE(resizeError);

	return true;
}

// The probe classifies a file by its first ProbeSampleSize bytes, and growing a file fills it
// with zeros, so the sample is written as text first or its NUL bytes would read as Binary.
inline bool TryWriteLargeTextFile(const std::filesystem::path& path, std::uint64_t fileSize)
{
	WriteFileBytes(path, std::string(Core::ProbeSampleSize, 'x'));

#ifdef CORE_PLATFORM_WINDOWS
	if (!MarkFileSparse(path))
	{
		return false;
	}
#endif

	std::error_code resizeError;
	std::filesystem::resize_file(path, fileSize, resizeError);
	REQUIRE_FALSE(resizeError);

	return true;
}
