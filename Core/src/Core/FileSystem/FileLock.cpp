#include "corepch.h"

#include "Core/FileSystem/FileLock.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Win32Error.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	FileLock::FileLock(void* handle) : m_Handle(handle) {}

	FileLock::FileLock(FileLock&& other) noexcept : m_Handle(std::exchange(other.m_Handle, nullptr)) {}

	FileLock& FileLock::operator=(FileLock&& other) noexcept
	{
		if (this != &other)
		{
			Release();
			m_Handle = std::exchange(other.m_Handle, nullptr);
		}

		return *this;
	}

	FileLock::~FileLock()
	{
		Release();
	}

	void FileLock::Release()
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (m_Handle != nullptr)
		{
			::CloseHandle(std::exchange(m_Handle, nullptr));
		}
#endif
	}

	Result<FileLock> FileLock::TryAcquire([[maybe_unused]] const std::filesystem::path& lockFile)
	{
#ifdef CORE_PLATFORM_WINDOWS
		// No sharing at all is what makes a second opener fail; delete-on-close is what makes a crash release it.
		const DWORD noSharing = 0;
		const HANDLE handle = ::CreateFileW(lockFile.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, noSharing, nullptr,
											OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);

		if (handle == INVALID_HANDLE_VALUE)
		{
			const DWORD openErrorCode = ::GetLastError();
			const bool heldElsewhere = openErrorCode == ERROR_SHARING_VIOLATION || openErrorCode == ERROR_ACCESS_DENIED;

			if (heldElsewhere)
			{
				return Error{.Code = ErrorCode::AccessDenied,
							 .Message = std::format("'{}' is held by another process", ToUtf8(lockFile))};
			}

			return Error{.Code = ErrorCodeFromWin32(openErrorCode),
						 .Message = std::format("cannot create the lock '{}' (error {})", ToUtf8(lockFile), openErrorCode)};
		}

		return FileLock{handle};
#else
		return Error{.Code = ErrorCode::Unsupported, .Message = "file locks are not built for this platform yet"};
#endif
	}
}
