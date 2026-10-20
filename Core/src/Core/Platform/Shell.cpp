#include "corepch.h"

#include "Core/Platform/Shell.h"

#include "Core/FileSystem/FileSystem.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <shellapi.h>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		constexpr INT_PTR kLargestShellExecuteErrorCode = 32;

		std::wstring SelectArgumentFor(const std::filesystem::path& path)
		{
			// explorer.exe understands only backslashes after /select. Handed forward slashes, it
			// silently ignores the argument and opens a default folder instead.
			std::filesystem::path preferredPath = path;
			preferredPath.make_preferred();

			std::wstring pathText = preferredPath.wstring();

			// A backslash just before the closing quote would escape it, so a path ending in one, a
			// drive root such as "D:\", needs it doubled to survive.
			const bool endsInBackslash = !pathText.empty() && pathText.back() == L'\\';

			if (endsInBackslash)
			{
				pathText.push_back(L'\\');
			}

			// Without the quotes Explorer splits the argument on the comma and on spaces, and opens the
			// wrong folder or none at all.
			return L"/select,\"" + pathText + L"\"";
		}
	}
#endif

	Result<void> Shell::OpenFolder(const std::filesystem::path& folder)
	{
		if (!PathExists(folder))
		{
			return Error{.Code = ErrorCode::NotFound,
						 .Message = std::format("'{}' no longer exists", ToUtf8(folder))};
		}

#ifdef CORE_PLATFORM_WINDOWS
		std::filesystem::path preferredFolder = folder;
		preferredFolder.make_preferred();

		const HINSTANCE launchResult = ::ShellExecuteW(nullptr, L"explore", preferredFolder.c_str(), nullptr,
													   nullptr, SW_SHOWNORMAL);

		const bool launched = reinterpret_cast<INT_PTR>(launchResult) > kLargestShellExecuteErrorCode;

		if (!launched)
		{
			return Error{.Code = ErrorCode::IoFailure,
						 .Message = std::format("cannot open the file browser for '{}'", ToUtf8(folder))};
		}

		return Result<void>::Ok();
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = std::format("opening a file browser for '{}' is not implemented on "
											"this platform",
											ToUtf8(folder))};
#endif
	}

	Result<void> Shell::RevealInFileBrowser(const std::filesystem::path& path)
	{
		if (!PathExists(path))
		{
			return Error{.Code = ErrorCode::NotFound,
						 .Message = std::format("'{}' no longer exists", ToUtf8(path))};
		}

#ifdef CORE_PLATFORM_WINDOWS
		const std::wstring arguments = SelectArgumentFor(path);

		const HINSTANCE launchResult = ::ShellExecuteW(nullptr, L"open", L"explorer.exe",
													   arguments.c_str(), nullptr, SW_SHOWNORMAL);

		const bool launched = reinterpret_cast<INT_PTR>(launchResult) > kLargestShellExecuteErrorCode;

		if (!launched)
		{
			return Error{.Code = ErrorCode::IoFailure,
						 .Message = std::format("cannot open the file browser for '{}'", ToUtf8(path))};
		}

		return Result<void>::Ok();
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = std::format("opening a file browser for '{}' is not implemented on "
											"this platform",
											ToUtf8(path))};
#endif
	}

	Result<void> Shell::OpenInBrowser(std::string_view url)
	{
		constexpr std::string_view kSecureScheme = "https://";

		if (!url.starts_with(kSecureScheme))
		{
			return Error{.Code = ErrorCode::InvalidArgument, .Message = std::format("'{}' is not an https address", url)};
		}

#ifdef CORE_PLATFORM_WINDOWS
		const std::wstring address = ToUtf16(url);
		const HINSTANCE launchResult = ::ShellExecuteW(nullptr, L"open", address.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		const bool launched = reinterpret_cast<INT_PTR>(launchResult) > kLargestShellExecuteErrorCode;

		if (!launched)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot open '{}' in the browser", url)};
		}

		return Result<void>::Ok();
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = std::format("opening '{}' in a browser is not implemented on this platform", url)};
#endif
	}
}
