#include "corepch.h"

#include <cctype>

#include "Core/FileSystem/FileSystem.h"

#include "Core/Base/Narrow.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/WslSharePath.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string_view>
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

#ifdef CORE_PLATFORM_WINDOWS
		constexpr std::wstring_view kEveryEntryPattern = L"*";
		constexpr std::wstring_view kCurrentDirectoryEntry = L".";
		constexpr std::wstring_view kParentDirectoryEntry = L"..";
		constexpr int kBitsInLowSizeHalf = 32;

		struct FindClosing
		{
			void operator()(HANDLE findHandle) const { ::FindClose(findHandle); }
		};

		using FindHandle = std::unique_ptr<void, FindClosing>;

		bool IsDotEntry(const WIN32_FIND_DATAW& found)
		{
			const std::wstring_view name{found.cFileName};
			return name == kCurrentDirectoryEntry || name == kParentDirectoryEntry;
		}

		bool HasHiddenAttribute(DWORD attributes)
		{
			return (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
		}

		bool HasDirectoryAttribute(DWORD attributes)
		{
			return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		}

		std::uint64_t SizeOf(const WIN32_FIND_DATAW& found)
		{
			return (static_cast<std::uint64_t>(found.nFileSizeHigh) << kBitsInLowSizeHalf) | found.nFileSizeLow;
		}

		bool IsLinkToFollow(const WIN32_FIND_DATAW& found)
		{
			const bool isReparsePoint = (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
			const DWORD reparseTag = found.dwReserved0;

			return isReparsePoint && (reparseTag == IO_REPARSE_TAG_SYMLINK || reparseTag == IO_REPARSE_TAG_MOUNT_POINT);
		}

		struct WhatALinkNames
		{
			bool IsDirectory = false;
			std::uint64_t Size = 0;
		};

		// WARNING: A link lists as what it points at, and a dangling one as an empty file, never an unreadable folder.
		WhatALinkNames FollowLink(const std::filesystem::path& link)
		{
			std::error_code statusError;
			const std::filesystem::file_status target = std::filesystem::status(link, statusError);

			if (statusError)
			{
				return WhatALinkNames{};
			}

			if (std::filesystem::is_directory(target))
			{
				return WhatALinkNames{.IsDirectory = true};
			}

			std::error_code sizeError;
			const std::uintmax_t targetSize = std::filesystem::file_size(link, sizeError);

			return WhatALinkNames{.Size = sizeError ? 0 : static_cast<std::uint64_t>(targetSize)};
		}

		DirectoryEntry EntryFound(const std::filesystem::path& folder, const WIN32_FIND_DATAW& found)
		{
			const std::filesystem::path fileName{found.cFileName};
			DirectoryEntry entry{
				.Path = folder / fileName,
				.Name = ToUtf8(fileName),
				.IsDirectory = HasDirectoryAttribute(found.dwFileAttributes),
				.IsHidden = HasHiddenAttribute(found.dwFileAttributes),
			};

			if (IsLinkToFollow(found))
			{
				const WhatALinkNames target = FollowLink(entry.Path);
				entry.IsDirectory = target.IsDirectory;
				entry.Size = target.Size;
				return entry;
			}

			entry.Size = entry.IsDirectory ? 0 : SizeOf(found);
			return entry;
		}

		std::error_code WindowsError(DWORD win32Error)
		{
			return std::error_code{static_cast<int>(win32Error), std::system_category()};
		}
#else
		bool IsHidden(const std::filesystem::path& path)
		{
			const std::string name = ToUtf8(path.filename());
			return !name.empty() && name.front() == '.';
		}
#endif

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

#ifdef CORE_PLATFORM_WINDOWS
	// WARNING: The enumeration hands back each entry's attributes; asking again per entry costs a second on a big folder.
	Result<std::vector<DirectoryEntry>> ReadDirectory(const std::filesystem::path& path)
	{
		if (path.empty())
		{
			const std::error_code error = WindowsError(ERROR_PATH_NOT_FOUND);
			return Error{.Code = TranslateError(error), .Message = std::format("cannot read directory '': {}", error.message())};
		}

		const std::filesystem::path everyEntry = path / kEveryEntryPattern;
		WIN32_FIND_DATAW found{};

		const HANDLE firstFound = ::FindFirstFileExW(everyEntry.c_str(), FindExInfoBasic, &found, FindExSearchNameMatch,
													 nullptr, FIND_FIRST_EX_LARGE_FETCH);

		if (firstFound == INVALID_HANDLE_VALUE)
		{
			const DWORD openError = ::GetLastError();

			// WARNING: Only an empty drive root, which has no "." or "..", answers this, and it is an empty listing.
			if (openError == ERROR_FILE_NOT_FOUND)
			{
				return std::vector<DirectoryEntry>{};
			}

			const std::error_code error = WindowsError(openError);
			return Error{.Code = TranslateError(error), .Message = std::format("cannot read directory '{}': {}", ToUtf8(path), error.message())};
		}

		const FindHandle findHandle{firstFound};
		std::vector<DirectoryEntry> entries;
		bool holdsAnEntry = true;

		while (holdsAnEntry)
		{
			if (!IsDotEntry(found))
			{
				entries.push_back(EntryFound(path, found));
			}

			holdsAnEntry = ::FindNextFileW(findHandle.get(), &found) != FALSE;
		}

		const DWORD enumerationEnd = ::GetLastError();

		if (enumerationEnd != ERROR_NO_MORE_FILES)
		{
			const std::error_code error = WindowsError(enumerationEnd);
			return Error{.Code = TranslateError(error), .Message = std::format("cannot enumerate '{}': {}", ToUtf8(path), error.message())};
		}

		return entries;
	}
#else
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
#endif

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

	int CompareNamesNaturally(std::string_view left, std::string_view right)
	{
		const auto isDigit = [](char character) { return character >= '0' && character <= '9'; };

		const auto digitRunEnd = [&isDigit](std::string_view text, std::size_t from) {
			while (from < text.size() && isDigit(text[from]))
			{
				++from;
			}

			return from;
		};

		std::size_t leftIndex = 0;
		std::size_t rightIndex = 0;
		int leadingZeroTieBreak = 0;

		while (leftIndex < left.size() && rightIndex < right.size())
		{
			if (isDigit(left[leftIndex]) && isDigit(right[rightIndex]))
			{
				const std::size_t leftEnd = digitRunEnd(left, leftIndex);
				const std::size_t rightEnd = digitRunEnd(right, rightIndex);

				std::string_view leftDigits = left.substr(leftIndex, leftEnd - leftIndex);
				std::string_view rightDigits = right.substr(rightIndex, rightEnd - rightIndex);

				const std::size_t leftRun = leftDigits.size();
				const std::size_t rightRun = rightDigits.size();

				while (leftDigits.size() > 1 && leftDigits.front() == '0')
				{
					leftDigits.remove_prefix(1);
				}

				while (rightDigits.size() > 1 && rightDigits.front() == '0')
				{
					rightDigits.remove_prefix(1);
				}

				if (leftDigits.size() != rightDigits.size())
				{
					return leftDigits.size() < rightDigits.size() ? -1 : 1;
				}

				if (const int byDigits = leftDigits.compare(rightDigits); byDigits != 0)
				{
					return byDigits < 0 ? -1 : 1;
				}

				if (leadingZeroTieBreak == 0 && leftRun != rightRun)
				{
					leadingZeroTieBreak = leftRun < rightRun ? -1 : 1;
				}

				leftIndex = leftEnd;
				rightIndex = rightEnd;
				continue;
			}

			const int leftLowered = LoweredByCurrentLocale(left[leftIndex]);
			const int rightLowered = LoweredByCurrentLocale(right[rightIndex]);

			if (leftLowered != rightLowered)
			{
				return leftLowered < rightLowered ? -1 : 1;
			}

			++leftIndex;
			++rightIndex;
		}

		const bool leftEnded = leftIndex == left.size();
		const bool rightEnded = rightIndex == right.size();

		if (leftEnded != rightEnded)
		{
			return leftEnded ? -1 : 1;
		}

		return leadingZeroTieBreak;
	}

	std::filesystem::path NormalizePath(const std::filesystem::path& path)
	{
		std::filesystem::path normalized = path.lexically_normal();
		normalized.make_preferred();
		return normalized;
	}

	PathKeyText PathKey(const std::filesystem::path& path)
	{
		PathKeyText key = NormalizeDirectory(CanonicalWslSharePath(path)).native();

#ifdef CORE_PLATFORM_WINDOWS
		// A place in WSL is on a Linux file system, which tells Makefile from makefile: its key
		// keeps the case, and only its spelling is made one (ADR 0696).
		const bool isInWsl = ParseWslSharePath(path).has_value();

		// The invariant locale folds case as the file system does, where the user's locale would give
		// "i" a different capital under Turkish. Upper-casing never changes the length of UTF-16 text,
		// which RelativeToKey relies on to cut the path at the length of its base's key.
		if (!key.empty() && !isInWsl)
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
#endif
		// Elsewhere the key keeps its case: Linux file systems tell Makefile from makefile, and
		// a key that folded them would call two files one.

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
