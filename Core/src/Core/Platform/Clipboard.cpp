#include "corepch.h"

#include "Core/Platform/Clipboard.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	bool ClipboardHasText()
	{
#ifdef CORE_PLATFORM_WINDOWS
		// Needs no OpenClipboard, so another process holding the clipboard can neither block nor
		// fail it. Windows synthesises CF_UNICODETEXT from CF_TEXT, so text put there only as
		// CF_TEXT is reported too.
		return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
#else
		return true;
#endif
	}
}
