#include "corepch.h"

#include "Core/Text/SharedFileReader.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Win32Error.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	struct SharedFileReader::OpenFile
	{
		HANDLE Handle = INVALID_HANDLE_VALUE;
		std::filesystem::path Path;

		OpenFile(HANDLE handle, std::filesystem::path path) : Handle(handle), Path(std::move(path)) {}

		OpenFile(const OpenFile&) = delete;
		OpenFile& operator=(const OpenFile&) = delete;
		OpenFile(OpenFile&&) = delete;
		OpenFile& operator=(OpenFile&&) = delete;

		~OpenFile() { ::CloseHandle(Handle); }
	};

	Result<SharedFileReader> SharedFileReader::Open(const std::filesystem::path& path)
	{
		constexpr DWORD kShareWithEveryone = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
		constexpr LPSECURITY_ATTRIBUTES kDefaultSecurity = nullptr;
		constexpr HANDLE kNoTemplate = nullptr;

		const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ, kShareWithEveryone, kDefaultSecurity, OPEN_EXISTING,
											FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, kNoTemplate);

		if (handle == INVALID_HANDLE_VALUE)
		{
			return Error{.Code = ErrorCodeFromWin32(::GetLastError()), .Message = "Cannot open file: " + ToUtf8(path)};
		}

		return SharedFileReader{std::make_unique<OpenFile>(handle, path)};
	}

	Result<std::size_t> SharedFileReader::ReadInto(std::span<char> destination)
	{
		const std::size_t largestSingleRead = std::numeric_limits<DWORD>::max();
		const DWORD bytesWanted = static_cast<DWORD>(std::min(destination.size(), largestSingleRead));
		DWORD bytesRead = 0;
		constexpr LPOVERLAPPED kNotOverlapped = nullptr;

		if (!::ReadFile(m_File->Handle, destination.data(), bytesWanted, &bytesRead, kNotOverlapped))
		{
			return Error{.Code = ErrorCodeFromWin32(::GetLastError()), .Message = "Cannot read file: " + ToUtf8(m_File->Path)};
		}

		return static_cast<std::size_t>(bytesRead);
	}
#else
	struct SharedFileReader::OpenFile
	{
		std::ifstream Stream;
		std::filesystem::path Path;
	};

	Result<SharedFileReader> SharedFileReader::Open(const std::filesystem::path& path)
	{
		std::unique_ptr<OpenFile> file = std::make_unique<OpenFile>();
		file->Stream.open(path, std::ios::binary);
		file->Path = path;

		if (!file->Stream.is_open())
		{
			std::error_code existsError;
			const bool fileIsGone = !std::filesystem::exists(path, existsError) && !existsError;

			return Error{.Code = fileIsGone ? ErrorCode::NotFound : ErrorCode::AccessDenied, .Message = "Cannot open file: " + ToUtf8(path)};
		}

		return SharedFileReader{std::move(file)};
	}

	Result<std::size_t> SharedFileReader::ReadInto(std::span<char> destination)
	{
		m_File->Stream.read(destination.data(), static_cast<std::streamsize>(destination.size()));

		if (m_File->Stream.bad())
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = "Cannot read file: " + ToUtf8(m_File->Path)};
		}

		return static_cast<std::size_t>(m_File->Stream.gcount());
	}
#endif

	SharedFileReader::SharedFileReader(std::unique_ptr<OpenFile> file) : m_File(std::move(file))
	{
	}

	SharedFileReader::SharedFileReader(SharedFileReader&& other) noexcept = default;

	SharedFileReader& SharedFileReader::operator=(SharedFileReader&& other) noexcept = default;

	SharedFileReader::~SharedFileReader() = default;
}
