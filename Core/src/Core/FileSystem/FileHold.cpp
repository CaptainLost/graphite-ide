#include "corepch.h"

#include "Core/FileSystem/FileHold.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Win32Error.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	FileHold::FileHold(UniqueHandle handle) : m_Handle(std::move(handle)) {}

	Result<FileHold> FileHold::HoldOpen([[maybe_unused]] const std::filesystem::path& file)
	{
#ifdef CORE_PLATFORM_WINDOWS
		// Sharing read alone, without delete, is what refuses a rename of the file or of any directory above it.
		const HANDLE handle = ::CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
											FILE_ATTRIBUTE_NORMAL, nullptr);

		if (handle == INVALID_HANDLE_VALUE)
		{
			const DWORD openErrorCode = ::GetLastError();

			return Error{.Code = ErrorCodeFromWin32(openErrorCode),
						 .Message = std::format("cannot hold '{}' open (error {})", ToUtf8(file), openErrorCode)};
		}

		return FileHold{UniqueHandle{handle}};
#else
		return Error{.Code = ErrorCode::Unsupported, .Message = "file holds are not built for this platform yet"};
#endif
	}
}
