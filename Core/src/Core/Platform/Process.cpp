#include "corepch.h"

#include "Core/Platform/Process.h"

#ifdef CORE_PLATFORM_WINDOWS
    #include <windows.h>
#endif

namespace core::platform
{
    Result<std::filesystem::path> ExecutableDirectory()
    {
#ifdef CORE_PLATFORM_WINDOWS
        // GetModuleFileNameW gives no way to ask for the required length: it fills what
        // it can and reports truncation through ERROR_INSUFFICIENT_BUFFER, so grow until
        // it fits rather than trusting MAX_PATH, which long paths exceed.
        std::wstring buffer(MAX_PATH, L'\0');

        while (true)
        {
            const DWORD written =
                GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

            if (written == 0)
                return Error{ErrorCode::IoFailure, "cannot determine the executable path"};

            if (written < buffer.size())
            {
                buffer.resize(written);
                break;
            }

            if (buffer.size() >= 32768)
                return Error{ErrorCode::IoFailure, "the executable path is implausibly long"};

            buffer.resize(buffer.size() * 2);
        }

        return std::filesystem::path{buffer}.parent_path();
#else
        return Error{ErrorCode::Unsupported,
                     "locating the executable is not implemented on this platform"};
#endif
    }

    bool IsForeground()
    {
#ifdef CORE_PLATFORM_WINDOWS
        const HWND foreground = ::GetForegroundWindow();

        // Null when the foreground window belongs to another desktop, or during the
        // moment a window is being switched. Not ours either way.
        if (foreground == nullptr)
            return false;

        DWORD owner = 0;
        ::GetWindowThreadProcessId(foreground, &owner);

        return owner == ::GetCurrentProcessId();
#else
        return true;
#endif
    }
}
