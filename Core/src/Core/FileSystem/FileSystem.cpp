#include "corepch.h"

#include <cctype>

#include "Core/FileSystem/FileSystem.h"

#include "Core/Base/Narrow.h"

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
		constexpr std::size_t kReadChunkBytes = 64 * 1024;

#ifdef CORE_PLATFORM_WINDOWS
		// Without WC_ERR_INVALID_CHARS or MB_ERR_INVALID_CHARS the Win32 conversions write U+FFFD for a
		// lone surrogate or bytes that are not UTF-8, where std::filesystem's u8string throws
		// std::system_error. Callers rely on a recognisable name coming back, not an exception or an
		// empty string.
		constexpr DWORD kReplaceInvalidSequences = 0;

		constexpr int kMeasureOnly = 0;
		constexpr LPARAM kNoSortHandle = 0;
#endif

		ErrorCode TranslateError(const std::error_code& error)
		{
			if (error == std::errc::no_such_file_or_directory)
			{
				return ErrorCode::NotFound;
			}

			if (error == std::errc::permission_denied)
			{
				return ErrorCode::AccessDenied;
			}

			if (error == std::errc::not_a_directory)
			{
				return ErrorCode::InvalidArgument;
			}

			return ErrorCode::IoFailure;
		}

		bool IsKeyWithin(const PathKeyText& key, const PathKeyText& rootKey)
		{
			constexpr std::filesystem::path::value_type separator = std::filesystem::path::preferred_separator;

			if (rootKey.empty() || !key.starts_with(rootKey))
			{
				return false;
			}

			const bool keyIsTheRoot = key.size() == rootKey.size();

			if (keyIsTheRoot)
			{
				return true;
			}

			const bool rootEndsInSeparator = rootKey.back() == separator;
			const bool separatorFollowsRoot = key[rootKey.size()] == separator;

			return rootEndsInSeparator || separatorFollowsRoot;
		}

		bool IsHidden(const std::filesystem::path& path)
		{
#ifdef CORE_PLATFORM_WINDOWS
			const DWORD attributes = ::GetFileAttributesW(path.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES)
			{
				return false;
			}

			return (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
#else
			const std::string name = ToUtf8(path.filename());
			return !name.empty() && name.front() == '.';
#endif
		}

		int LoweredByCurrentLocale(char character)
		{
			return std::tolower(static_cast<unsigned char>(character));
		}

		bool IsOverLimit(std::uintmax_t byteCount, const std::optional<std::uintmax_t>& maximumBytes)
		{
			return maximumBytes.has_value() && byteCount > *maximumBytes;
		}

		Error FileTooLargeError(const std::filesystem::path& path, std::uintmax_t maximumBytes)
		{
			return Error{.Code = ErrorCode::TooLarge, .Message = "File is larger than " + std::to_string(maximumBytes) + " bytes: " + ToUtf8(path)};
		}
	}

	std::string ToUtf8(const std::filesystem::path& path)
	{
#ifdef CORE_PLATFORM_WINDOWS
		const std::wstring& utf16Path = path.native();
		if (utf16Path.empty())
		{
			return {};
		}

		const int utf16Length = Narrow<int>(utf16Path.size());
		const int utf8Length = ::WideCharToMultiByte(CP_UTF8, kReplaceInvalidSequences, utf16Path.data(), utf16Length,
													 nullptr, kMeasureOnly, nullptr, nullptr);

		std::string utf8Text(static_cast<std::size_t>(utf8Length), '\0');
		::WideCharToMultiByte(CP_UTF8, kReplaceInvalidSequences, utf16Path.data(), utf16Length, utf8Text.data(),
							  utf8Length, nullptr, nullptr);
		return utf8Text;
#else
		const std::u8string utf8Path = path.u8string();
		return std::string{reinterpret_cast<const char*>(utf8Path.data()), utf8Path.size()};
#endif
	}

	std::string ToGenericUtf8(const std::filesystem::path& path)
	{
		std::string utf8Text = ToUtf8(path);
		std::ranges::replace(utf8Text, '\\', '/');
		return utf8Text;
	}

#ifdef CORE_PLATFORM_WINDOWS
	std::wstring ToUtf16(std::string_view text)
	{
		if (text.empty())
		{
			return {};
		}

		const int textLength = Narrow<int>(text.size());
		const int utf16Length = ::MultiByteToWideChar(CP_UTF8, kReplaceInvalidSequences, text.data(), textLength, nullptr, kMeasureOnly);

		std::wstring utf16Text(static_cast<std::size_t>(utf16Length), L'\0');
		::MultiByteToWideChar(CP_UTF8, kReplaceInvalidSequences, text.data(), textLength, utf16Text.data(), utf16Length);
		return utf16Text;
	}
#endif

	std::filesystem::path FromUtf8(std::string_view text)
	{
#ifdef CORE_PLATFORM_WINDOWS
		return std::filesystem::path{ToUtf16(text)};
#else
		const std::u8string utf8Text{reinterpret_cast<const char8_t*>(text.data()), text.size()};
		return std::filesystem::path{utf8Text};
#endif
	}

	Result<std::vector<DirectoryEntry>> ReadDirectory(const std::filesystem::path& path)
	{
		std::error_code error;

		// Not skip_permission_denied: it turns an unreadable directory into an empty listing,
		// indistinguishable from a directory that really is empty.
		std::filesystem::directory_iterator iterator{
			path, std::filesystem::directory_options::none, error};

		if (error)
		{
			return Error{.Code = TranslateError(error), .Message = std::format("cannot read directory '{}': {}", ToUtf8(path), error.message())};
		}

		std::vector<DirectoryEntry> entries;

		for (; iterator != std::filesystem::directory_iterator{}; iterator.increment(error))
		{
			if (error)
			{
				return Error{.Code = TranslateError(error), .Message = std::format("cannot enumerate '{}': {}", ToUtf8(path), error.message())};
			}

			const std::filesystem::path& entryPath = iterator->path();
			DirectoryEntry entry{.Path = entryPath, .Name = ToUtf8(entryPath.filename())};

			std::error_code classifyError;
			entry.IsDirectory = iterator->is_directory(classifyError);

			if (classifyError)
			{
				return Error{.Code = TranslateError(classifyError), .Message = std::format("cannot classify '{}': {}", ToUtf8(entry.Path), classifyError.message())};
			}

			entry.IsHidden = IsHidden(entry.Path);

			if (!entry.IsDirectory)
			{
				// The size is informational, unlike the kind, so an unreadable size lists the entry with its default of 0
				// rather than failing the whole folder.
				std::error_code sizeError;
				const std::uintmax_t fileSize = iterator->file_size(sizeError);

				if (!sizeError)
				{
					entry.Size = fileSize;
				}
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
		const std::size_t sharedLength = std::min(left.size(), right.size());

		for (std::size_t byteIndex = 0; byteIndex < sharedLength; ++byteIndex)
		{
			const int leftLowered = LoweredByCurrentLocale(left[byteIndex]);
			const int rightLowered = LoweredByCurrentLocale(right[byteIndex]);

			if (leftLowered != rightLowered)
			{
				return leftLowered < rightLowered ? -1 : 1;
			}
		}

		if (left.size() == right.size())
		{
			return 0;
		}

		return left.size() < right.size() ? -1 : 1;
	}

	std::filesystem::path NormalizePath(const std::filesystem::path& path)
	{
		std::filesystem::path normalized = path.lexically_normal();
		normalized.make_preferred();
		return normalized;
	}

	PathKeyText PathKey(const std::filesystem::path& path)
	{
		PathKeyText key = NormalizeDirectory(path).native();

#ifdef CORE_PLATFORM_WINDOWS
		// The invariant locale folds case as the file system does, where the user's locale would give
		// "i" a different capital under Turkish. Upper-casing never changes the length of UTF-16 text,
		// which RelativeToKey relies on to cut the path at the length of its base's key.
		if (!key.empty())
		{
			const int keyLength = Narrow<int>(key.size());
			std::wstring folded(key.size(), L'\0');

			const int foldedLength = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, key.data(), keyLength,
													 folded.data(), keyLength, nullptr, nullptr, kNoSortHandle);
			const bool foldedWholeKey = foldedLength == keyLength;

			if (foldedWholeKey)
			{
				key = std::move(folded);
			}
		}
#else
		for (char& character : key)
		{
			character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
		}
#endif

		return key;
	}

	bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
	{
		return PathKey(left) == PathKey(right);
	}

	std::filesystem::path NormalizeDirectory(const std::filesystem::path& path)
	{
		std::filesystem::path normalized = NormalizePath(path);

		const bool endsInSeparator = !normalized.has_filename();
		const bool isRoot = !normalized.has_relative_path();

		if (endsInSeparator && !isRoot)
		{
			normalized = normalized.parent_path();
		}

		return normalized;
	}

	std::optional<std::filesystem::path> RelativeTo(const std::filesystem::path& path,
													const std::filesystem::path& base)
	{
		if (base.empty())
		{
			return std::nullopt;
		}

		return RelativeToKey(path, PathKey(base));
	}

	std::optional<std::filesystem::path> RelativeToKey(const std::filesystem::path& path,
													   const PathKeyText& baseKey)
	{
		const PathKeyText pathKey = PathKey(path);

		if (!IsKeyWithin(pathKey, baseKey))
		{
			return std::nullopt;
		}

		const std::size_t baseEndInNormalizedPath = baseKey.size();
		std::filesystem::path::string_type relativeSpelling = NormalizeDirectory(path).native().substr(baseEndInNormalizedPath);

		while (!relativeSpelling.empty() && relativeSpelling.front() == std::filesystem::path::preferred_separator)
		{
			relativeSpelling.erase(0, 1);
		}

		return relativeSpelling.empty() ? std::filesystem::path{"."} : std::filesystem::path{std::move(relativeSpelling)};
	}

	bool IsSameOrWithin(const std::filesystem::path& path, const std::filesystem::path& root)
	{
		const bool rootNamesAPlace = !root.empty() && root.is_absolute();
		const bool pathNamesAPlace = path.is_absolute();

		if (!rootNamesAPlace || !pathNamesAPlace)
		{
			return false;
		}

		return IsKeyWithin(PathKey(path), PathKey(root));
	}

	Result<std::string> ReadFileBytes(const std::filesystem::path& path,
									  std::optional<std::uintmax_t> maximumBytes)
	{
		std::error_code sizeError;
		const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);

		if (sizeError)
		{
			return Error{.Code = TranslateError(sizeError), .Message = "Cannot read " + ToUtf8(path) + ": " + sizeError.message()};
		}

		if (IsOverLimit(fileSize, maximumBytes))
		{
			return FileTooLargeError(path, *maximumBytes);
		}

		std::ifstream file(path, std::ios::binary);

		if (!file)
		{
			return Error{.Code = ErrorCode::AccessDenied, .Message = "Cannot open " + ToUtf8(path)};
		}

		std::string bytes;
		std::array<char, kReadChunkBytes> chunk;

		while (file)
		{
			file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
			bytes.append(chunk.data(), static_cast<std::size_t>(file.gcount()));

			// Checked again while reading, because the file can grow after its size was taken.
			if (IsOverLimit(bytes.size(), maximumBytes))
			{
				return FileTooLargeError(path, *maximumBytes);
			}
		}

		if (file.bad())
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = "Read failed: " + ToUtf8(path)};
		}

		return bytes;
	}

	// std::filesystem::exists without an error_code throws when it cannot examine a path, as when access is denied.
	bool PathExists(const std::filesystem::path& path)
	{
		std::error_code failure;

		return std::filesystem::exists(path, failure) && !failure;
	}
}
