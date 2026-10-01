#pragma once

// Private to Core's .cpp files: never include this from a public header, because it
// pulls in <windows.h>.

#ifdef CORE_PLATFORM_WINDOWS

#include "Core/Base/Result.h"

#include <windows.h>

namespace Core {

	// The one mapping from a Win32 error to an ErrorCode. A sharing violation counts as
	// AccessDenied: from the caller's side another process holding the file is a refusal
	// to let this one touch it, and retrying immediately will not help.
	inline ErrorCode ErrorCodeFromWin32(DWORD error)
	{
		switch (error)
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
