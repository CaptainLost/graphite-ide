#pragma once

#include "Core/Base/Result.h"
#include "Core/Platform/UniqueHandle.h"

#include <filesystem>

namespace Core {

	// Others may read the file while it is held, but nobody can write, rename or delete it, or rename a directory above it.
	class FileHold
	{
	public:
		static Result<FileHold> HoldOpen(const std::filesystem::path& file);

	private:
		explicit FileHold(UniqueHandle handle);

		UniqueHandle m_Handle;
	};
}
