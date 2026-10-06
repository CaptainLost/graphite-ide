#include "corepch.h"

#include "Core/Platform/Process.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <ShlObj.h>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		constexpr std::size_t kLongestWindowsPathBufferCharacters = 32768;

		constexpr DWORD kEmptyValueLengthWithTerminator = 1;

		// Asked of the shell rather than read from APPDATA or LOCALAPPDATA: those variables are
		// inherited and can be overridden or missing in a child's environment, while the known
		// folder is what the profile actually has, redirected folders included.
		Result<std::filesystem::path> KnownFolder(REFKNOWNFOLDERID folderId, const char* folderDescription)
		{
			PWSTR folderPathText = nullptr;
			const HRESULT lookupResult = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &folderPathText);

			// The buffer is the caller's to free on failure as well as on success.
			std::filesystem::path folderPath = SUCCEEDED(lookupResult) ? std::filesystem::path{folderPathText}
																	   : std::filesystem::path{};
			CoTaskMemFree(folderPathText);

			if (FAILED(lookupResult))
			{
				return Error{.Code = ErrorCode::NotFound, .Message = std::format("cannot locate the {} folder", folderDescription)};
			}

			return folderPath;
		}
	}
#endif

	Result<std::filesystem::path> ExecutableDirectory()
	{
#ifdef CORE_PLATFORM_WINDOWS
		// GetModuleFileNameW cannot be asked for the length it needs: it fills what it can and
		// reports truncation through ERROR_INSUFFICIENT_BUFFER. So the buffer grows until the path
		// fits, rather than trusting MAX_PATH, which long paths exceed.
		const HMODULE runningExecutable = nullptr;
		std::wstring executablePath(MAX_PATH, L'\0');

		while (true)
		{
			const DWORD charactersWritten =
				GetModuleFileNameW(runningExecutable, executablePath.data(), static_cast<DWORD>(executablePath.size()));

			if (charactersWritten == 0)
			{
				return Error{.Code = ErrorCode::IoFailure, .Message = "cannot determine the executable path"};
			}

			const bool pathFits = charactersWritten < executablePath.size();

			if (pathFits)
			{
				executablePath.resize(charactersWritten);
				break;
			}

			if (executablePath.size() >= kLongestWindowsPathBufferCharacters)
			{
				return Error{.Code = ErrorCode::IoFailure, .Message = "the executable path is implausibly long"};
			}

			executablePath.resize(executablePath.size() * 2);
		}

		return std::filesystem::path{executablePath}.parent_path();
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = "locating the executable is not implemented on this platform"};
#endif
	}

	std::optional<std::filesystem::path> EnvironmentPath([[maybe_unused]] const wchar_t* name)
	{
#ifdef CORE_PLATFORM_WINDOWS
		// GetEnvironmentVariableW counts the terminator when it reports the length and leaves it out when it copies the value.
		const DWORD measureOnly = 0;
		const DWORD lengthWithTerminator = GetEnvironmentVariableW(name, nullptr, measureOnly);

		const bool unsetOrEmpty = lengthWithTerminator <= kEmptyValueLengthWithTerminator;

		if (unsetOrEmpty)
		{
			return std::nullopt;
		}

		std::wstring value(lengthWithTerminator, L'\0');
		const DWORD charactersWritten = GetEnvironmentVariableW(name, value.data(), lengthWithTerminator);

		const bool wholeValueRead = charactersWritten != 0 && charactersWritten < lengthWithTerminator;

		if (!wholeValueRead)
		{
			return std::nullopt;
		}

		value.resize(charactersWritten);
		return std::filesystem::path{value};
#else
		return std::nullopt;
#endif
	}

	Result<std::filesystem::path> HomeDirectory()
	{
		std::optional<std::filesystem::path> home = EnvironmentPath(L"USERPROFILE");

		if (!home.has_value())
		{
			return Error{.Code = ErrorCode::NotFound, .Message = "USERPROFILE is not set"};
		}

		std::error_code ignoredError;
		const bool namesADirectory = std::filesystem::is_directory(*home, ignoredError);

		if (!namesADirectory)
		{
			return Error{.Code = ErrorCode::NotFound, .Message = "USERPROFILE does not name a directory"};
		}

		return std::move(*home);
	}

	Result<std::filesystem::path> RoamingDataDirectory()
	{
#ifdef CORE_PLATFORM_WINDOWS
		return KnownFolder(FOLDERID_RoamingAppData, "roaming application data");
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = "the roaming data directory is not implemented on this platform"};
#endif
	}

	Result<std::filesystem::path> LocalDataDirectory()
	{
#ifdef CORE_PLATFORM_WINDOWS
		return KnownFolder(FOLDERID_LocalAppData, "local application data");
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = "the local data directory is not implemented on this platform"};
#endif
	}

	Result<std::filesystem::path> SystemFontsDirectory()
	{
#ifdef CORE_PLATFORM_WINDOWS
		return KnownFolder(FOLDERID_Fonts, "system fonts");
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = "the system fonts directory is not implemented on this platform"};
#endif
	}

	bool IsProcessInForeground()
	{
#ifdef CORE_PLATFORM_WINDOWS
		const HWND foregroundWindow = ::GetForegroundWindow();

		// GetForegroundWindow answers null while the foreground belongs to another desktop or a
		// window switch is under way, and neither is ours.
		if (foregroundWindow == nullptr)
		{
			return false;
		}

		DWORD ownerProcessId = 0;
		::GetWindowThreadProcessId(foregroundWindow, &ownerProcessId);

		return ownerProcessId == ::GetCurrentProcessId();
#else
		return true;
#endif
	}
}
