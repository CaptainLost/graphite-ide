#pragma once

#include "Core/Base/Result.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>

namespace Core {

	// WARNING: Opened so other programs can still delete, rename or write the file while it is read.
	class SharedFileReader
	{
	public:
		static Result<SharedFileReader> Open(const std::filesystem::path& path);

		SharedFileReader(SharedFileReader&& other) noexcept;
		SharedFileReader& operator=(SharedFileReader&& other) noexcept;
		SharedFileReader(const SharedFileReader&) = delete;
		SharedFileReader& operator=(const SharedFileReader&) = delete;
		~SharedFileReader();

		Result<std::size_t> ReadInto(std::span<char> destination);

	private:
		struct OpenFile;

		explicit SharedFileReader(std::unique_ptr<OpenFile> file);

		std::unique_ptr<OpenFile> m_File;
	};
}
