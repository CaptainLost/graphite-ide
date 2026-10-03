#include "corepch.h"

#include <cctype>

#include "Core/Fs/FileSystem.h"

#include "Core/Base/Ascii.h"

#include <algorithm>
#include <array>
#include <fstream>
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

		// Whether the place `key` names is `rootKey`'s or lies below it, both made by PathKey. A
		// root ending in a separator ("D:\") already marks a boundary; any other has to be followed
		// by one, or "D:\Repos2" would count as inside "D:\Repos".
		bool IsKeyWithin(const std::filesystem::path::string_type& key,
						 const std::filesystem::path::string_type& rootKey)
		{
			constexpr auto separator = std::filesystem::path::preferred_separator;

			if (rootKey.empty() || !key.starts_with(rootKey))
			{
				return false;
			}

			return key.size() == rootKey.size() || rootKey.back() == separator || key[rootKey.size()] == separator;
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

	// Both directions convert with the Win32 calls rather than through std::filesystem, whose
	// u8string() and u8string constructor throw std::system_error on a lone surrogate or on bytes
	// that are not UTF-8 (ADR 0004). Without the strict flags, the Win32 calls write U+FFFD there
	// instead, so a name that cannot be spelled exactly still comes out as a recognisable one.
	std::string ToUtf8(const std::filesystem::path& path)
	{
#ifdef CORE_PLATFORM_WINDOWS
		const std::wstring& wide = path.native();
		if (wide.empty())
		{
			return {};
		}

		const int wideLength = static_cast<int>(wide.size());
		const int length = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);

		std::string text(static_cast<std::size_t>(length), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, text.data(), length, nullptr, nullptr);
		return text;
#else
		const std::u8string utf8 = path.u8string();
		return std::string{reinterpret_cast<const char*>(utf8.data()), utf8.size()};
#endif
	}

	std::string ToGenericUtf8(const std::filesystem::path& path)
	{
		std::string text = ToUtf8(path);
		std::ranges::replace(text, '\\', '/');
		return text;
	}

	std::filesystem::path FromUtf8(std::string_view text)
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (text.empty())
		{
			return {};
		}

		const int textLength = static_cast<int>(text.size());
		const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), textLength, nullptr, 0);

		std::wstring wide(static_cast<std::size_t>(length), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, text.data(), textLength, wide.data(), length);
		return std::filesystem::path{std::move(wide)};
#else
		const std::u8string utf8{reinterpret_cast<const char8_t*>(text.data()), text.size()};
		return std::filesystem::path{utf8};
#endif
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

	std::string LoweredExtension(const std::filesystem::path& path)
	{
		return Lowered(ToUtf8(path.extension()));
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

	std::filesystem::path::string_type PathKey(const std::filesystem::path& path)
	{
		std::filesystem::path::string_type key = NormalizeDirectory(path).native();

#ifdef CORE_PLATFORM_WINDOWS
		// The invariant upper-casing table, as the file system folds names, rather than the user's
		// locale: a Turkish locale would otherwise give "i" a different capital from everyone
		// else's. Upper-casing never changes the length of UTF-16 text, so the key stays aligned
		// with the path it came from, which RelativeTo relies on.
		if (!key.empty())
		{
			const int length = static_cast<int>(key.size());
			std::wstring folded(key.size(), L'\0');

			if (::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, key.data(), length, folded.data(), length,
								nullptr, nullptr, 0) == length)
			{
				key = std::move(folded);
			}
		}
#else
		std::ranges::transform(key, key.begin(),
							   [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
#endif

		return key;
	}

	bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
	{
		return PathKey(left) == PathKey(right);
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
		{
			return std::nullopt;
		}

		const auto pathKey = PathKey(path);
		const auto baseKey = PathKey(base);

		if (!IsKeyWithin(pathKey, baseKey))
		{
			return std::nullopt;
		}

		// The key is the normalized path upper-cased, character for character, so its length
		// marks where the base ends in the normalized spelling too - and the remainder keeps the
		// case it was written in.
		auto rest = NormalizeDirectory(path).native().substr(baseKey.size());

		while (!rest.empty() && rest.front() == std::filesystem::path::preferred_separator)
		{
			rest.erase(0, 1);
		}

		return rest.empty() ? std::filesystem::path{"."} : std::filesystem::path{std::move(rest)};
	}

	bool IsSameOrWithin(const std::filesystem::path& path, const std::filesystem::path& root)
	{
		if (root.empty() || !root.is_absolute() || !path.is_absolute())
		{
			return false;
		}

		return IsKeyWithin(PathKey(path), PathKey(root));
	}

	Result<std::string> ReadFileBytes(const std::filesystem::path& path,
									  std::optional<std::uintmax_t> maximumBytes)
	{
		const auto tooLarge = [&path, maximumBytes]()
		{
			return Error{ErrorCode::TooLarge,
						 "File is larger than " + std::to_string(*maximumBytes) +
							 " bytes: " + ToUtf8(path)};
		};

		std::error_code code;
		const std::uintmax_t size = std::filesystem::file_size(path, code);

		if (code)
		{
			return Error{TranslateError(code), "Cannot read " + ToUtf8(path) + ": " + code.message()};
		}

		if (maximumBytes && size > *maximumBytes)
		{
			return tooLarge();
		}

		std::ifstream in(path, std::ios::binary);

		if (!in)
		{
			return Error{ErrorCode::AccessDenied, "Cannot open " + ToUtf8(path)};
		}

		std::string bytes;
		std::array<char, 64 * 1024> chunk;

		while (in)
		{
			in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
			bytes.append(chunk.data(), static_cast<std::size_t>(in.gcount()));

			if (maximumBytes && bytes.size() > *maximumBytes)
			{
				return tooLarge();
			}
		}

		if (in.bad())
		{
			return Error{ErrorCode::IoFailure, "Read failed: " + ToUtf8(path)};
		}

		return bytes;
	}
}
