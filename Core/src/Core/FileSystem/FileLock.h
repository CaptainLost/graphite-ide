#pragma once

#include "Core/Base/Result.h"
#include "Core/Platform/UniqueHandle.h"

#include <filesystem>

namespace Core {

	// The lock file is deleted when its holder lets go, and a process that dies lets go too, so a stale file never
	// locks anyone out.
	class FileLock
	{
	public:
		static Result<FileLock> TryAcquire(const std::filesystem::path& lockFile);

	private:
		explicit FileLock(UniqueHandle handle);

		UniqueHandle m_Handle;
	};
}
