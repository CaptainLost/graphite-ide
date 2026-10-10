#pragma once

#include "Core/Base/Result.h"

#include <filesystem>

namespace Core {

	// Others may read the file while it is held, but nobody can write, rename or delete it, or rename a directory above it.
	class FileHold
	{
	public:
		FileHold(const FileHold&) = delete;
		FileHold& operator=(const FileHold&) = delete;
		FileHold(FileHold&& other) noexcept;
		FileHold& operator=(FileHold&& other) noexcept;
		~FileHold();

		static Result<FileHold> HoldOpen(const std::filesystem::path& file);

	private:
		explicit FileHold(void* handle);

		void Release();

		void* m_Handle = nullptr;
	};
}
