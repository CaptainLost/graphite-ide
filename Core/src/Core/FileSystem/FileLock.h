#pragma once

#include "Core/Base/Result.h"

#include <filesystem>

namespace Core {

	// The lock file is deleted when its holder lets go, and a process that dies lets go too, so a stale file never
	// locks anyone out.
	class FileLock
	{
	public:
		FileLock(const FileLock&) = delete;
		FileLock& operator=(const FileLock&) = delete;
		FileLock(FileLock&& other) noexcept;
		FileLock& operator=(FileLock&& other) noexcept;
		~FileLock();

		static Result<FileLock> TryAcquire(const std::filesystem::path& lockFile);

	private:
		explicit FileLock(void* handle);

		void Release();

		void* m_Handle = nullptr;
	};
}
