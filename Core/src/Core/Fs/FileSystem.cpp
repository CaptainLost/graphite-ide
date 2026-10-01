#include "corepch.h"

#include <cctype>

#include "Core/Fs/FileSystem.h"

#include <algorithm>
#include <system_error>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	namespace {
		ErrorCode TranslateError(const std::error_code& code)
		{
			if (code == std::errc::no_such_file_or_directory)
				return ErrorCode::NotFound;

			if (code == std::errc::permission_denied)
				return ErrorCode::AccessDenied;

			if (code == std::errc::not_a_directory)
				return ErrorCode::InvalidArgument;

			return ErrorCode::IoFailure;
		}

		bool IsHidden(const std::filesystem::path& path)
		{
#ifdef CORE_PLATFORM_WINDOWS
			const DWORD attributes = ::GetFileAttributesW(path.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES)
				return false;

			return (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
#else
			const std::string name = ToUtf8(path.filename());
			return !name.empty() && name.front() == '.';
#endif
		}
	}

	std::string ToUtf8(const std::filesystem::path& path)
	{
		const std::u8string utf8 = path.u8string();
		return std::string{reinterpret_cast<const char*>(utf8.data()), utf8.size()};
	}

	std::string ToGenericUtf8(const std::filesystem::path& path)
	{
		std::string text = ToUtf8(path);
		std::ranges::replace(text, '\\', '/');
		return text;
	}

	std::filesystem::path FromUtf8(std::string_view text)
	{
		const std::u8string utf8{reinterpret_cast<const char8_t*>(text.data()), text.size()};
		return std::filesystem::path{utf8};
	}

	Result<std::vector<DirectoryEntry>> ReadDirectory(const std::filesystem::path& path)
	{
		std::error_code code;

		// directory_options::none rather than skip_permission_denied, which turns an
		// unreadable directory into an empty listing indistinguishable from a genuinely
		// empty one.
		std::filesystem::directory_iterator iterator{
			path, std::filesystem::directory_options::none, code};

		if (code)
		{
			return Error{TranslateError(code),
						 std::format("cannot read directory '{}': {}", ToUtf8(path), code.message())};
		}

		std::vector<DirectoryEntry> entries;

		for (; iterator != std::filesystem::directory_iterator{}; iterator.increment(code))
		{
			if (code)
			{
				return Error{TranslateError(code),
							 std::format("cannot enumerate '{}': {}", ToUtf8(path), code.message())};
			}

			DirectoryEntry entry;
			entry.Path = iterator->path();
			entry.Name = ToUtf8(iterator->path().filename());

			std::error_code statusCode;
			entry.IsDirectory = iterator->is_directory(statusCode);

			if (statusCode)
			{
				return Error{TranslateError(statusCode),
							 std::format("cannot classify '{}': {}", ToUtf8(entry.Path),
										 statusCode.message())};
			}

			entry.IsHidden = IsHidden(entry.Path);

			if (!entry.IsDirectory)
			{
				std::error_code sizeCode;
				const std::uintmax_t size = iterator->file_size(sizeCode);
				entry.Size = sizeCode ? 0 : static_cast<std::uint64_t>(size);
			}

			entries.push_back(std::move(entry));
		}

		return entries;
	}

	int CompareNames(std::string_view left, std::string_view right)
	{
		const std::size_t count = std::min(left.size(), right.size());

		for (std::size_t i = 0; i < count; ++i)
		{
			const int a = std::tolower(static_cast<unsigned char>(left[i]));
			const int b = std::tolower(static_cast<unsigned char>(right[i]));

			if (a != b)
				return a < b ? -1 : 1;
		}

		if (left.size() == right.size())
			return 0;

		return left.size() < right.size() ? -1 : 1;
	}

	std::filesystem::path NormalizePath(const std::filesystem::path& path)
	{
		std::filesystem::path normalized = path.lexically_normal();
		normalized.make_preferred();
		return normalized;
	}

	bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
	{
		return CompareNames(ToUtf8(left), ToUtf8(right)) == 0;
	}

	std::filesystem::path NormalizeDirectory(const std::filesystem::path& path)
	{
		std::filesystem::path normalized = path.lexically_normal();
		normalized.make_preferred();

		// lexically_normal leaves a trailing separator in place, which shows up as an empty
		// final element — has_filename() is false exactly then — and makes the path unequal
		// to the same directory spelled without it. has_relative_path() guards the roots:
		// parent_path() of `D:/` is `D:`, a drive-relative path naming a different place.
		if (!normalized.has_filename() && normalized.has_relative_path())
			normalized = normalized.parent_path();

		return normalized;
	}

	std::optional<std::filesystem::path> RelativeTo(const std::filesystem::path& path,
													const std::filesystem::path& base)
	{
		if (base.empty())
			return std::nullopt;

		const std::filesystem::path relative = path.lexically_relative(base);

		if (relative.empty() || *relative.begin() == "..")
			return std::nullopt;

		return relative;
	}

	bool IsSameOrWithin(const std::filesystem::path& path, const std::filesystem::path& root)
	{
		if (root.empty() || !root.is_absolute() || !path.is_absolute())
		{
			return false;
		}

		const std::filesystem::path normalizedPath = NormalizeDirectory(path);
		const std::filesystem::path normalizedRoot = NormalizeDirectory(root);

		auto inPath = normalizedPath.begin();

		for (const std::filesystem::path& component : normalizedRoot)
		{
			// A drive root's trailing separator is a component of its own ("D:", "\"); an empty
			// final element never appears here because NormalizeDirectory removed it.
			if (inPath == normalizedPath.end() || CompareNames(ToUtf8(*inPath), ToUtf8(component)) != 0)
			{
				return false;
			}

			++inPath;
		}

		return true;
	}
}
