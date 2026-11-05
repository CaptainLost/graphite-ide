#include "Git/History/CommitLabels.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Strings.h"
#include "Core/Time/TimeZone.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <string_view>

namespace Graphite {

	namespace {

		constexpr long long kSecondsPerMinute = 60;
		constexpr long long kSecondsPerHour = 3600;
		constexpr long long kSecondsPerDay = 86400;
		constexpr long long kSecondsPerMonth = 2592000;
		constexpr long long kSecondsPerYear = 31536000;

		std::string AgeText(long long unitCount, std::string_view unit)
		{
			return Core::Counted(unitCount, unit) + " ago";
		}

		char UppercaseFirstLetter(std::string_view word)
		{
			return word.empty()
					   ? '?'
					   : static_cast<char>(std::toupper(static_cast<unsigned char>(word.front())));
		}

		// WARNING: Git normally trims signature names, but a stray space left on one throws off the first and last word split and gives a blank initial.
		std::string_view WithoutSurroundingSpaces(std::string_view text)
		{
			return Core::WithoutSurrounding(text, " ");
		}
	}

	std::string RelativeAge(std::chrono::sys_seconds when, std::chrono::sys_seconds now)
	{
		const std::chrono::seconds elapsed = now - when;

		if (elapsed < std::chrono::seconds{kSecondsPerMinute})
		{
			return "just now";
		}

		const long long seconds = elapsed.count();

		if (seconds < kSecondsPerHour)
		{
			return AgeText(seconds / kSecondsPerMinute, "minute");
		}

		if (seconds < kSecondsPerDay)
		{
			return AgeText(seconds / kSecondsPerHour, "hour");
		}

		if (seconds < kSecondsPerMonth)
		{
			return AgeText(seconds / kSecondsPerDay, "day");
		}

		if (seconds < kSecondsPerYear)
		{
			return AgeText(seconds / kSecondsPerMonth, "month");
		}

		return AgeText(seconds / kSecondsPerYear, "year");
	}

	std::string ShortAge(std::chrono::sys_seconds when, std::chrono::sys_seconds now)
	{
		constexpr long long kDaysPerWeek = 7;
		constexpr long long kDaysPerMonth = 30;
		constexpr long long kDaysPerYear = 365;
		constexpr long long kDaysBeforeWeeks = 14;
		constexpr long long kDaysBeforeMonths = 60;

		const long long seconds = std::max(0LL, static_cast<long long>((now - when).count()));

		if (seconds < kSecondsPerHour)
		{
			return std::format("{}m", std::max(1LL, seconds / kSecondsPerMinute));
		}

		if (seconds < kSecondsPerDay)
		{
			return std::format("{}h", seconds / kSecondsPerHour);
		}

		const long long days = seconds / kSecondsPerDay;

		if (days < kDaysBeforeWeeks)
		{
			return std::format("{}d", days);
		}

		if (days < kDaysBeforeMonths)
		{
			return std::format("{}w", days / kDaysPerWeek);
		}

		if (days < kDaysPerYear)
		{
			return std::format("{}mo", days / kDaysPerMonth);
		}

		return std::format("{}y", days / kDaysPerYear);
	}

	int AvatarHue(std::string_view email)
	{
		constexpr std::uint32_t kFnvOffset = 2166136261u;
		constexpr std::uint32_t kFnvPrime = 16777619u;
		constexpr std::uint32_t kDegrees = 360u;

		std::uint32_t hash = kFnvOffset;

		for (const char letter : email)
		{
			hash ^= static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(letter)));
			hash *= kFnvPrime;
		}

		return static_cast<int>(hash % kDegrees);
	}

	ImU32 AvatarFill(int hue)
	{
		constexpr float kSaturation = 0.34f;
		constexpr float kLightness = 0.36f;
		constexpr float kDegreesPerSector = 60.0f;
		constexpr float kByteRange = 255.0f;

		const float chroma = (1.0f - std::fabs(2.0f * kLightness - 1.0f)) * kSaturation;
		const float sector = static_cast<float>(hue) / kDegreesPerSector;
		const float second = chroma * (1.0f - std::fabs(std::fmod(sector, 2.0f) - 1.0f));
		const float base = kLightness - chroma / 2.0f;

		float red = 0.0f;
		float green = 0.0f;
		float blue = 0.0f;

		switch (static_cast<int>(sector) % 6)
		{
		case 0:
			red = chroma;
			green = second;
			break;
		case 1:
			red = second;
			green = chroma;
			break;
		case 2:
			green = chroma;
			blue = second;
			break;
		case 3:
			green = second;
			blue = chroma;
			break;
		case 4:
			red = second;
			blue = chroma;
			break;
		default:
			red = chroma;
			blue = second;
			break;
		}

		const auto toByte = [&](float channel) {
			return static_cast<int>(std::lround((channel + base) * kByteRange));
		};

		return IM_COL32(toByte(red), toByte(green), toByte(blue), 255);
	}

	const std::chrono::time_zone* ReaderTimeZone()
	{
		static const std::chrono::time_zone* const zone = Core::LocalTimeZone();

		return zone;
	}

	std::string AuthorInitials(const Core::Signature& signature)
	{
		const std::string_view name = WithoutSurroundingSpaces(signature.Name);

		if (name.empty())
		{
			return std::string{UppercaseFirstLetter(signature.Email)};
		}

		const std::size_t firstSpace = name.find(' ');

		if (firstSpace == std::string_view::npos)
		{
			return std::string{UppercaseFirstLetter(name)};
		}

		const std::size_t lastWordStart = name.find_last_of(' ') + 1;

		return std::string{UppercaseFirstLetter(name)} +
			   std::string{UppercaseFirstLetter(name.substr(lastWordStart))};
	}

	std::string AuthorName(const Core::Signature& signature)
	{
		const std::string_view name = WithoutSurroundingSpaces(signature.Name);

		if (!name.empty())
		{
			return std::string{name};
		}

		const std::string_view email = WithoutSurroundingSpaces(signature.Email);
		const std::size_t atSignOffset = email.find('@');
		const std::string_view localPart =
			atSignOffset == std::string_view::npos ? email : email.substr(0, atSignOffset);

		return localPart.empty() ? std::string{"unknown"} : std::string{localPart};
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

	const char* ChangeName(Core::FileChange change)
	{
		switch (change)
		{
		case Core::FileChange::Added:
			return "Added";
		case Core::FileChange::Modified:
			return "Modified";
		case Core::FileChange::Deleted:
			return "Deleted";
		case Core::FileChange::Renamed:
			return "Renamed";
		case Core::FileChange::Copied:
			return "Copied";
		case Core::FileChange::TypeChanged:
			return "Type changed";
		case Core::FileChange::Unmodified:
			return nullptr;
		}

		return nullptr;
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
		for (std::size_t fileIndex = 0; fileIndex < diff.Files.size(); ++fileIndex)
		{
			const Core::FileDiff& file = diff.Files[fileIndex];

			if (file.NewPath == path || file.OldPath == path)
			{
				return fileIndex;
			}
		}

		return std::nullopt;
	}
}
