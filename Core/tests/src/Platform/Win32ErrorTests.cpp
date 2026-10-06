#include <doctest/doctest.h>

#ifdef CORE_PLATFORM_WINDOWS

#include "Core/Platform/Win32Error.h"

using Core::ErrorCode;
using Core::ErrorCodeFromWin32;

namespace {
	constexpr DWORD kErrorTheMappingDoesNotKnow = 0xDEADBEEF;
}

TEST_CASE("A Win32 error maps to the ErrorCode callers act on and a sharing violation is access denied since another process holding the file is a refusal that an immediate retry will not overcome")
{
	CHECK(ErrorCodeFromWin32(ERROR_ACCESS_DENIED) == ErrorCode::AccessDenied);
	CHECK(ErrorCodeFromWin32(ERROR_SHARING_VIOLATION) == ErrorCode::AccessDenied);
	CHECK(ErrorCodeFromWin32(ERROR_FILE_NOT_FOUND) == ErrorCode::NotFound);
	CHECK(ErrorCodeFromWin32(ERROR_PATH_NOT_FOUND) == ErrorCode::NotFound);
	CHECK(ErrorCodeFromWin32(ERROR_DISK_FULL) == ErrorCode::IoFailure);
	CHECK(ErrorCodeFromWin32(kErrorTheMappingDoesNotKnow) == ErrorCode::IoFailure);
}

#endif
