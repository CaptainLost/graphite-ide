#include "corepch.h"

#include "Core/Platform/Shell.h"

#include "Core/Fs/FileSystem.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <shellapi.h>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		std::wstring SelectArgumentFor(const std::filesystem::path& path)
		{
			// explorer.exe only understands backslashes after /select. Handed forward slashes
			// it silently ignores the argument and opens a default folder instead.
			std::filesystem::path native = path;
			native.make_preferred();

			std::wstring quoted = native.wstring();

			// A backslash immediately before the closing quote would escape it, so a path
			// ending in one — a drive root such as "D:\" — needs it doubled to survive.
			if (!quoted.empty() && quoted.back() == L'\\')
			{
				quoted.push_back(L'\\');
			}

			// The quotes are load-bearing: without them Explorer splits the argument on the
			// comma and on spaces, and opens the wrong folder or none at all.
			return L"/select,\"" + quoted + L"\"";
		}
	}
#endif

	Result<void> Shell::RevealInFileBrowser(const std::filesystem::path& path)
	{
		std::error_code code;
		if (!std::filesystem::exists(path, code))
		{
			return Error{ErrorCode::NotFound,
						 std::format("'{}' no longer exists", ToUtf8(path))};
		}

#ifdef CORE_PLATFORM_WINDOWS
		const std::wstring arguments = SelectArgumentFor(path);

		const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", L"explorer.exe",
												 arguments.c_str(), nullptr, SW_SHOWNORMAL);

		// ShellExecuteW returns a value greater than 32 on success; smaller values are
		// error codes stuffed into the pointer.
		if (reinterpret_cast<INT_PTR>(result) <= 32)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot open the file browser for '{}'", ToUtf8(path))};
		}

		return Result<void>::Ok();
#else
		return Error{ErrorCode::Unsupported,
					 std::format("opening a file browser for '{}' is not implemented on "
								 "this platform",
								 ToUtf8(path))};
#endif
	}
}