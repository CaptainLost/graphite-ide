#include "corepch.h"

#include "Core/FileSystem/RelativePath.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

namespace Core {

	namespace {

		constexpr std::string_view kCharactersWindowsForbids = "<>:\"|?*";

		constexpr std::array<std::string_view, 22> kDeviceNames{
			"con",	"prn",	"aux",	"nul",	"com1", "com2", "com3", "com4", "com5", "com6", "com7",
			"com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};

		bool IsForbiddenCharacter(char character)
		{
			const bool isControlCharacter = static_cast<unsigned char>(character) < 0x20;
			return isControlCharacter || kCharactersWindowsForbids.find(character) != std::string_view::npos;
		}

		bool NamesADevice(std::string_view segment)
		{
			const std::string_view stem = segment.substr(0, segment.find('.'));

			for (const std::string_view deviceName : kDeviceNames)
			{
				if (EqualsIgnoringCase(stem, deviceName))
				{
					return true;
				}
			}

			return false;
		}

		// Windows drops a trailing dot or space from a name, so "bin." lands in "bin" and "..." climbs.
		bool IsSafeSegment(std::string_view segment)
		{
			const bool endsAsWindowsWouldTrim = segment.ends_with('.') || segment.ends_with(' ');
			return !endsAsWindowsWouldTrim && !NamesADevice(segment);
		}
	}

	bool IsSafeRelativePath(std::string_view path)
	{
		if (path.empty() || IsPathSeparator(path.front()))
		{
			return false;
		}

		for (const char character : path)
		{
			if (IsForbiddenCharacter(character))
			{
				return false;
			}
		}

		std::size_t segmentStart = 0;

		while (segmentStart <= path.size())
		{
			const std::size_t separator = path.find_first_of("/\\", segmentStart);
			const std::size_t segmentEnd = separator == std::string_view::npos ? path.size() : separator;
			const std::string_view segment = path.substr(segmentStart, segmentEnd - segmentStart);

			if (!segment.empty() && !IsSafeSegment(segment))
			{
				return false;
			}

			segmentStart = segmentEnd + 1;
		}

		return true;
	}
}
