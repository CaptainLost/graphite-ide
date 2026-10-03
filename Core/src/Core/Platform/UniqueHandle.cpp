#include "corepch.h"

#include "Core/Platform/UniqueHandle.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	void UniqueHandle::Reset(void* handle)
	{
		[[maybe_unused]] void* const old = std::exchange(m_Handle, Normalise(handle));

#ifdef CORE_PLATFORM_WINDOWS
		if (old != nullptr)
		{
			::CloseHandle(old);
		}
#else
		// Nothing on another platform hands out a Win32 handle, so nothing can be held.
#endif
	}
}
