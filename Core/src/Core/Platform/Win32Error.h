#pragma once

// Private to Core's .cpp files: it pulls in <windows.h>, so a public header must never include it.

#ifdef CORE_PLATFORM_WINDOWS

#include "Core/Base/Result.h"

#include <windows.h>

namespace Core {

	inline ErrorCode ErrorCodeFromWin32(DWORD win32ErrorCode)
	{
		switch (win32ErrorCode)
		{
		case ERROR_ACCESS_DENIED:
		case ERROR_SHARING_VIOLATION:
			return ErrorCode::AccessDenied;
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			return ErrorCode::NotFound;
		default:
			return ErrorCode::IoFailure;
		}
	}

}

#endif
