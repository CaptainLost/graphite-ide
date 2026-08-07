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

        std::wstring quoted = path.wstring();

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
