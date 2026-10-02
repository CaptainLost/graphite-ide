#include "corepch.h"

#include "Core/Platform/Clipboard.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	bool ClipboardHasText()
	{
#ifdef CORE_PLATFORM_WINDOWS
		// Needs no OpenClipboard, so it neither contends with another process holding the
		// clipboard nor fails while one does. CF_UNICODETEXT is also reported when only
		// CF_TEXT was put there, since Windows synthesises the one from the other.
		return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
#else
		return true;
#endif
	}
}
