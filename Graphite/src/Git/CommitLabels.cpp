#include "Git/CommitLabels.h"

#include <cctype>
#include <format>
#include <string_view>

namespace Graphite {

	namespace {

		std::string Count(long long value, std::string_view unit)
		{
			return value == 1 ? std::format("1 {} ago", unit)
							  : std::format("{} {}s ago", value, unit);
		}

		char UpperFirst(std::string_view word)
		{
			return word.empty()
					   ? '?'
					   : static_cast<char>(std::toupper(static_cast<unsigned char>(word.front())));
		}

		// Git normally trims signature names, but a stray leading, trailing or sole space is
		// not impossible — and untrimmed it throws the first/last word split off, giving a
		// blank initial instead of the letter the reader expects.
		std::string_view Trim(std::string_view text)
		{
			const std::size_t begin = text.find_first_not_of(' ');

			if (begin == std::string_view::npos)
			{
				return {};
			}

			const std::size_t end = text.find_last_not_of(' ');

			return text.substr(begin, end - begin + 1);
		}

		// The single-character ellipsis (U+2026), as the three bytes its UTF-8 encoding
		// takes. Used whole or not at all — never split — so a cut result is never left
		// with a broken glyph of its own making.
		constexpr std::string_view Ellipsis = "\xE2\x80\xA6";

		// A UTF-8 continuation byte has the top two bits `10`. Stopping a cut on one of
		// these would split a multi-byte character in half and produce a broken glyph, so
		// every cut backs up to the nearest byte that is not one.
		bool IsUtf8Continuation(char byte)
		{
			return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
		}

		// The largest prefix length, no greater than maxBytes, that does not end inside a
		// multi-byte UTF-8 sequence. Plain ASCII text (the common case for both callers)
		// always gets maxBytes back unchanged.
		std::size_t Utf8SafeLength(std::string_view text, std::size_t maxBytes)
		{
			std::size_t length = maxBytes < text.size() ? maxBytes : text.size();

			// length == text.size() is the untouched, full string — always a safe boundary —
			// so the continuation check only runs, and only indexes text[length], while
			// length is still a valid in-range index.
			while (length > 0 && length < text.size() && IsUtf8Continuation(text[length]))
			{
				--length;
			}

			return length;
		}

		// Pads `text` with trailing spaces to exactly `width` bytes, or cuts it to `width`
		// bytes ending in the ellipsis, so every caller's column lines up regardless of how
		// long the underlying string is.
		//
		// `width` counts bytes, not display columns — a UTF-8 name that is cut still lands
		// on a whole-character boundary (see Utf8SafeLength), but a name that is merely
		// padded and contains multi-byte characters may occupy fewer display columns than
		// `width` suggests. Byte width is what the doctest suite pins, and is enough to
		// keep the monospace commit list aligned for the overwhelmingly common ASCII case.
		std::string PadOrCut(std::string_view text, std::size_t width)
		{
			if (text.size() <= width)
			{
				std::string result{text};
				result.append(width - text.size(), ' ');
				return result;
			}

			// Too narrow even for the ellipsis alone: emit a plain, UTF-8-safe prefix
			// instead of a partial ellipsis, which would be meaningless on its own. This
			// still returns exactly `width` bytes, padding out whatever the boundary search
			// gave up to reach it.
			if (width <= Ellipsis.size())
			{
				std::string result{text.substr(0, Utf8SafeLength(text, width))};
				result.append(width - result.size(), ' ');
				return result;
			}

			const std::size_t keep = Utf8SafeLength(text, width - Ellipsis.size());
			std::string result = std::string{text.substr(0, keep)} + std::string{Ellipsis};
			result.append(width - result.size(), ' ');
			return result;
		}

		// The name, or the email's local part, or "unknown" — never blank, since a blank
		// column tells the reader nothing.
		std::string AuthorDisplayName(const Core::Signature& who)
		{
			const std::string_view name = Trim(who.Name);

			if (!name.empty())
			{
				return std::string{name};
			}

			const std::string_view email = Trim(who.Email);
			const std::size_t at = email.find('@');
			const std::string_view local = at == std::string_view::npos ? email : email.substr(0, at);

			return local.empty() ? std::string{"unknown"} : std::string{local};
		}
	}

	std::string RelativeAge(std::chrono::sys_seconds when, std::chrono::sys_seconds now)
	{
		const auto elapsed = now - when;

		if (elapsed < std::chrono::seconds{60})
		{
			return "just now";
		}

		const long long seconds = elapsed.count();

		if (seconds < 3600)
		{
			return Count(seconds / 60, "minute");
		}

		if (seconds < 86400)
		{
			return Count(seconds / 3600, "hour");
		}

		if (seconds < 2592000)
		{
			return Count(seconds / 86400, "day");
		}

		if (seconds < 31536000)
		{
			return Count(seconds / 2592000, "month");
		}

		return Count(seconds / 31536000, "year");
	}

	std::string AuthorInitials(const Core::Signature& who)
	{
		const std::string_view name = Trim(who.Name);

		if (name.empty())
		{
			return std::string{UpperFirst(who.Email)};
		}

		const std::size_t firstEnd = name.find(' ');

		if (firstEnd == std::string_view::npos)
		{
			return std::string{UpperFirst(name)};
		}

		const std::size_t lastStart = name.find_last_of(' ') + 1;

		return std::string{UpperFirst(name)} + std::string{UpperFirst(name.substr(lastStart))};
	}

	std::string PaddedAuthor(const Core::Signature& who, std::size_t width)
	{
		return PadOrCut(AuthorDisplayName(who), width);
	}

	std::string PaddedAge(std::chrono::sys_seconds when, std::chrono::sys_seconds now, std::size_t width)
	{
		return PadOrCut(RelativeAge(when, now), width);
	}

	char ChangeLetter(Core::FileChange change)
	{
		switch (change)
		{
		case Core::FileChange::Added:
			return 'A';
		case Core::FileChange::Modified:
			return 'M';
		case Core::FileChange::Deleted:
			return 'D';
		case Core::FileChange::Renamed:
			return 'R';
		case Core::FileChange::Copied:
			return 'C';
		case Core::FileChange::TypeChanged:
			return 'T';
		case Core::FileChange::Unmodified:
			return ' ';
		}

		return ' ';
	}

	const std::filesystem::path& ShownPath(const Core::FileDiff& file)
	{
		return file.NewPath.empty() ? file.OldPath : file.NewPath;
	}
}
