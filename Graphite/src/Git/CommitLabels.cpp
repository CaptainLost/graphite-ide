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
		// takes. One code point, and — in every ordinary monospace face — one display
		// column, which is the unit PadOrCut below has to work in.
		constexpr std::string_view Ellipsis = "\xE2\x80\xA6";

		// A UTF-8 continuation byte has the top two bits `10`. A code point is one lead
		// byte followed by zero or more of these, so counting the non-continuation bytes
		// counts code points without decoding anything.
		bool IsUtf8Continuation(char byte)
		{
			return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
		}

		// How many UTF-8 code points `text` holds. This is what PadOrCut treats as the
		// display width: correct for the common case (ASCII, and any font that renders
		// every code point in one column), wrong for a character a font draws double-wide
		// (CJK, most emoji) — those still count as one here and will shift the column that
		// follows. That gap is real and out of scope for this change.
		std::size_t Utf8CodePointCount(std::string_view text)
		{
			std::size_t count = 0;

			for (const char byte : text)
			{
				if (!IsUtf8Continuation(byte))
				{
					++count;
				}
			}

			return count;
		}

		// The shortest byte-prefix of `text` that holds exactly `count` code points (or all
		// of `text`, if it holds fewer). Built by walking whole code points — a lead byte
		// plus its continuation bytes — one at a time, so the result never ends mid-sequence
		// the way a byte-offset cut could.
		std::string TakeCodePoints(std::string_view text, std::size_t count)
		{
			std::size_t index = 0;
			std::size_t taken = 0;

			while (index < text.size() && taken < count)
			{
				++index;

				while (index < text.size() && IsUtf8Continuation(text[index]))
				{
					++index;
				}

				++taken;
			}

			return std::string{text.substr(0, index)};
		}

		// Pads `text` with trailing spaces to exactly `width` display columns, or cuts it
		// to `width` columns ending in the ellipsis, so every caller's column lines up in
		// the monospace commit list regardless of how long or how encoded the underlying
		// string is.
		//
		// Width is counted in code points, not bytes: an all-ASCII name has one byte per
		// code point so the two coincide, but a cut that instead reserved bytes for the
		// (3-byte, 1-column) ellipsis rendered two columns short on any ordinary long name,
		// and padding a multi-byte name to a byte count left it short by one column per
		// accented character. Both are display-column bugs a byte-based test cannot see.
		std::string PadOrCut(std::string_view text, std::size_t width)
		{
			const std::size_t codePoints = Utf8CodePointCount(text);

			if (codePoints <= width)
			{
				std::string result{text};
				result.append(width - codePoints, ' ');
				return result;
			}

			if (width == 0)
			{
				return {};
			}

			// The ellipsis is exactly one column, so the prefix gets the rest of the
			// budget. TakeCodePoints(text, width - 1) always returns exactly width - 1
			// code points here (codePoints > width implies text has at least that many),
			// so the result is exactly width code points — no padding remainder to fill.
			return TakeCodePoints(text, width - 1) + std::string{Ellipsis};
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

	std::optional<std::size_t> IndexOfFile(const Core::Diff& diff,
										   const std::filesystem::path& path)
	{
		for (std::size_t index = 0; index < diff.Files.size(); ++index)
		{
			const Core::FileDiff& file = diff.Files[index];

			if (file.NewPath == path || file.OldPath == path)
			{
				return index;
			}
		}

		return std::nullopt;
	}
}
