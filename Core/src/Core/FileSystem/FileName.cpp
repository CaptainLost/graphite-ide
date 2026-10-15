#include "corepch.h"

#include "Core/FileSystem/FileName.h"

#include "Core/Base/Ascii.h"

#include <array>

namespace Core {

	namespace {

		constexpr std::string_view kCharactersWindowsForbids = "<>:\"/\\|?*";

		constexpr unsigned char kFirstPrintableCharacter = 0x20;

		constexpr std::array<std::string_view, 32> kDeviceNames{
			"con",	"prn",	"aux",	"nul",	"com1", "com2", "com3", "com4", "com5", "com6", "com7",
			"com8", "com9", "com0", "com¹", "com²", "com³", "lpt0", "lpt¹", "lpt²", "lpt³", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
			"conin$", "conout$"};

		bool HoldsForbiddenCharacter(std::string_view name)
		{
			for (const char character : name)
			{
				if (IsForbiddenInFileName(character))
				{
					return true;
				}
			}

			return false;
		}
	}

	bool IsValidFileName(std::string_view name)
	{
		if (ContainsOnlySpacesAndTabs(name) || HoldsForbiddenCharacter(name))
		{
			return false;
		}

		// WARNING: Windows strips a trailing dot or space from a name, so "notes." would silently create
		// "notes", and "..." would climb.
		const bool endsAsWindowsWouldTrim = name.ends_with('.') || name.ends_with(' ');

		return !endsAsWindowsWouldTrim && !NamesWindowsDevice(name);
	}

	bool IsForbiddenInFileName(char character)
	{
		const bool isControlCharacter = static_cast<unsigned char>(character) < kFirstPrintableCharacter;

		return isControlCharacter || kCharactersWindowsForbids.find(character) != std::string_view::npos;
	}

	bool NamesWindowsDevice(std::string_view name)
	{
		const std::string_view stem = WithoutTrailing(name.substr(0, name.find('.')), " ");

		for (const std::string_view deviceName : kDeviceNames)
		{
			if (EqualsIgnoringCase(stem, deviceName))
			{
				return true;
			}
		}

		return false;
	}
}
