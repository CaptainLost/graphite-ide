#include "corepch.h"

#include "Core/Platform/Shell.h"

#include "Core/Fs/FileSystem.h"

#include <windows.h>

#include <shellapi.h>

namespace core
{
    Result<void> Shell::RevealInFileBrowser(const std::filesystem::path& path)
    {
        std::error_code code;
        if (!std::filesystem::exists(path, code))
        {
            return Error{ErrorCode::NotFound,
                         std::format("'{}' no longer exists", fs::ToUtf8(path))};
        }

        // explorer.exe only understands backslashes after /select. Handed a path with
        // forward slashes — which is what a command-line argument or a settings file
        // usually carries — it silently ignores the argument and opens a default folder
        // instead, which looks to the user like the wrong folder opening at random.
        std::filesystem::path native = path;
        native.make_preferred();

        std::wstring quoted = native.wstring();

        // A backslash immediately before the closing quote would escape it, so a path
        // ending in one — a drive root such as "D:\" — needs it doubled to survive.
        if (!quoted.empty() && quoted.back() == L'\\')
            quoted.push_back(L'\\');

        // The quotes are load-bearing: without them Explorer splits the argument on the
        // comma and on spaces, and opens the wrong folder or none at all.
        const std::wstring arguments = L"/select,\"" + quoted + L"\"";

        const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", L"explorer.exe",
                                                 arguments.c_str(), nullptr, SW_SHOWNORMAL);

        // ShellExecuteW returns a value greater than 32 on success; smaller values are
        // error codes stuffed into the pointer.
        if (reinterpret_cast<INT_PTR>(result) <= 32)
        {
            return Error{ErrorCode::IoFailure,
                         std::format("cannot open the file browser for '{}'", fs::ToUtf8(path))};
        }

        return Result<void>::Ok();
    }
}
