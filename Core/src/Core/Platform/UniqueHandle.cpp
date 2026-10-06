#include "corepch.h"

#include "Core/Platform/UniqueHandle.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	void UniqueHandle::Reset(void* replacement)
	{
		[[maybe_unused]] void* const previousHandle = std::exchange(m_Handle, NullIfInvalid(replacement));

#ifdef CORE_PLATFORM_WINDOWS
		if (previousHandle != nullptr)
		{
			::CloseHandle(previousHandle);
		}
#else
		// Off Windows nothing hands out a Win32 handle, so nothing is ever held to close.
#endif
	}
}
