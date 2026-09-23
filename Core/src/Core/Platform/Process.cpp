#include "corepch.h"

#include "Core/Platform/Process.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

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

	std::optional<std::filesystem::path> EnvironmentPath(const wchar_t* name)
	{
#ifdef CORE_PLATFORM_WINDOWS
		// Asked twice: once for the length, once for the value. The length includes the
		// terminator; the second call's return value does not.
		const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);

		if (length <= 1)
			return std::nullopt;

		std::wstring value(length, L'\0');
		const DWORD written = GetEnvironmentVariableW(name, value.data(), length);

		if (written == 0 || written >= length)
			return std::nullopt;

		value.resize(written);
		return std::filesystem::path{value};
#else
		(void)name;
		return std::nullopt;
#endif
	}

	Result<std::filesystem::path> HomeDirectory()
	{
		std::optional<std::filesystem::path> home = EnvironmentPath(L"USERPROFILE");

		if (!home.has_value())
			return Error{ErrorCode::NotFound, "USERPROFILE is not set"};

		std::error_code status;

		if (!std::filesystem::is_directory(*home, status))
			return Error{ErrorCode::NotFound, "USERPROFILE does not name a directory"};

		return std::move(*home);
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