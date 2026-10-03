#include "Git/History/CommitLabels.h"

#include <cctype>
#include <format>
#include <string_view>
#include <vector>

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

		// A UTF-8 continuation byte has the top two bits `10`. A code point is one lead
		// byte followed by zero or more of these, so counting the non-continuation bytes
		// counts code points without decoding anything.
		bool IsUtf8Continuation(char byte)
		{
			return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
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

	std::string AuthorName(const Core::Signature& who)
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

	std::optional<std::size_t> CutToFit(std::string_view text, float maxWidth, float ellipsisWidth,
										const std::function<float(std::string_view)>& widthOf)
	{
		if (widthOf(text) <= maxWidth)
		{
			return std::nullopt;
		}

		// Every place a cut may fall: the start of each code point, and the end of the text
		// is not one of them, since the whole text was just found not to fit.
		//
		// The start of the text is always one, even when its first byte is a continuation
		// byte — a message in some other encoding, read as UTF-8. The search below needs a
		// floor it never has to measure, and a text that is not valid UTF-8 still gets cut
		// by width rather than all of it dropped.
		std::vector<std::size_t> boundaries{0};

		for (std::size_t index = 1; index < text.size(); ++index)
		{
			if (!IsUtf8Continuation(text[index]))
			{
				boundaries.push_back(index);
			}
		}

		// The last boundary whose prefix still fits beside the ellipsis. Searched by halving
		// rather than walked: this runs for every visible cell every frame, and measuring
		// each prefix of a long summary in turn would be quadratic in its length. The empty
		// prefix at boundary 0 is the floor, for a cell too narrow for anything else.
		const float budget = maxWidth - ellipsisWidth;
		std::size_t low = 0;
		std::size_t high = boundaries.size();

		while (high - low > 1)
		{
			const std::size_t middle = low + (high - low) / 2;

			if (widthOf(text.substr(0, boundaries[middle])) <= budget)
			{
				low = middle;
			}
			else
			{
				high = middle;
			}
		}

		std::size_t kept = boundaries[low];

		while (kept > 0 && text[kept - 1] == ' ')
		{
			--kept;
		}

		return kept;
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

	ImU32 ChangeColor(Core::FileChange change, const GitPalette& palette)
	{
		switch (change)
		{
		case Core::FileChange::Added:
			return palette.GutterAdded;
		case Core::FileChange::Deleted:
			return palette.GutterRemoved;
		case Core::FileChange::Modified:
		case Core::FileChange::Renamed:
		case Core::FileChange::Copied:
		case Core::FileChange::TypeChanged:
			return palette.GutterModified;
		case Core::FileChange::Unmodified:
			return 0;
		}

		return 0;
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
