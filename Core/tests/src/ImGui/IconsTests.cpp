#include <doctest/doctest.h>

#include "Core/ImGui/Icons.h"
#include "Core/Text/Utf8.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string_view>

using Core::IconForExtension;
using Core::UsedGlyphRanges;

namespace {
	constexpr int kEntriesPerRange = 2;

	ImWchar CodepointOf(std::string_view icon)
	{
		std::size_t offset = 0;
		const std::optional<char32_t> codepoint = Core::DecodeUtf8(icon, offset);
		REQUIRE(codepoint.has_value());
		return static_cast<ImWchar>(codepoint.value_or(Core::kReplacementCharacter));
	}
}

TEST_CASE("IconForExtension recognizes known source extensions")
{
	CHECK(std::strcmp(IconForExtension(".c"), ICON_LC_FILE_CODE) == 0);
	CHECK(std::strcmp(IconForExtension(".C"), ICON_LC_FILE_CODE) == 0);
	CHECK(std::strcmp(IconForExtension(".cpp"), ICON_LC_FILE_CODE) == 0);
	CHECK(std::strcmp(IconForExtension(".CPP"), ICON_LC_FILE_CODE) == 0);
	CHECK(std::strcmp(IconForExtension(".cc"), ICON_LC_FILE_CODE) == 0);
	CHECK(std::strcmp(IconForExtension(".cxx"), ICON_LC_FILE_CODE) == 0);
	CHECK(std::strcmp(IconForExtension(".h"), ICON_LC_FILE_CODE_2) == 0);
	CHECK(std::strcmp(IconForExtension(".H"), ICON_LC_FILE_CODE_2) == 0);
	CHECK(std::strcmp(IconForExtension(".hpp"), ICON_LC_FILE_CODE_2) == 0);
	CHECK(std::strcmp(IconForExtension(".hxx"), ICON_LC_FILE_CODE_2) == 0);
	CHECK(std::strcmp(IconForExtension(".cs"), ICON_LC_HASH) == 0);
	CHECK(std::strcmp(IconForExtension(".lua"), ICON_LC_FILE_CODE) == 0);
	CHECK(std::strcmp(IconForExtension(".json"), ICON_LC_FILE_JSON) == 0);
	CHECK(std::strcmp(IconForExtension(".md"), ICON_LC_FILE_TEXT) == 0);
	CHECK(std::strcmp(IconForExtension(".py"), ICON_LC_FILE_CODE) == 0);
}

TEST_CASE("IconForExtension tells a C++ header from a C++ source")
{
	CHECK(std::strcmp(IconForExtension(".h"), IconForExtension(".cpp")) != 0);
	CHECK(std::strcmp(IconForExtension("h"), IconForExtension(".h")) == 0);
}

TEST_CASE("IconForExtension tells a C# project from a C++ one")
{
	CHECK(std::strcmp(IconForExtension(".vcxproj"), ICON_LC_FILE_CODE) == 0);
	CHECK(std::strcmp(IconForExtension(".csproj"), ICON_LC_HASH) == 0);
	CHECK(std::strcmp(IconForExtension(".vcxproj"), IconForExtension(".csproj")) != 0);
}

TEST_CASE("IconForExtension falls back to a generic file icon")
{
	CHECK(std::strcmp(IconForExtension(".xyz"), ICON_LC_FILE) == 0);
	CHECK(std::strcmp(IconForExtension(""), ICON_LC_FILE) == 0);
}

TEST_CASE("IconForExtension accepts an extension with or without the leading dot")
{
	CHECK(std::strcmp(IconForExtension("cpp"), ICON_LC_FILE_CODE) == 0);
	CHECK(std::strcmp(IconForExtension("cpp"), IconForExtension(".cpp")) == 0);
}

TEST_CASE("UsedGlyphRanges covers every icon this app can display, zero-terminated")
{
	const ImWchar* ranges = UsedGlyphRanges();
	REQUIRE(ranges != nullptr);

	const char* icons[] = {
		ICON_LC_FILE,
		ICON_LC_FOLDER,
		ICON_LC_FOLDER_OPEN,
		ICON_LC_FILE_CODE,
		ICON_LC_FILE_CODE_2,
		ICON_LC_HASH,
		ICON_LC_FILE_JSON,
		ICON_LC_FILE_TEXT,
		ICON_LC_APP_WINDOW,
		ICON_LC_REFRESH_CW,
		ICON_LC_FOLDER_TREE,
		ICON_LC_FILE_DIFF,
		ICON_LC_GIT_BRANCH,
		ICON_LC_MESSAGE_SQUARE_TEXT,
		ICON_LC_SQUARE_TERMINAL,
		ICON_LC_CIRCLE_ALERT,
		ICON_LC_LINK,
		ICON_LC_TEXT_SEARCH,
		ICON_LC_SETTINGS,
		ICON_LC_BOT,
		ICON_LC_PENCIL,
		ICON_LC_GLOBE,
		ICON_LC_HAMMER,
		ICON_LC_BELL,
		ICON_LC_HOURGLASS,
		ICON_LC_CIRCLE_STOP,
		ICON_LC_WRENCH,
		ICON_LC_CIRCLE_HELP,
		ICON_LC_REFRESH_CCW,
		ICON_LC_USER,
		ICON_LC_PUZZLE,
		ICON_LC_NETWORK,
		ICON_LC_BRAIN,
		ICON_LC_ARROW_RIGHT,
		ICON_LC_IMAGE,
		ICON_LC_ARROW_UP,
		ICON_LC_ARROW_DOWN,
		ICON_LC_DOT,
		ICON_LC_ARCHIVE,
		ICON_LC_TERMINAL,
		ICON_LC_FILE_IMAGE,
	};

	for (const char* icon : icons)
	{
		const ImWchar codepoint = CodepointOf(icon);
		CAPTURE(static_cast<unsigned int>(codepoint));

		bool found = false;
		for (int rangeStart = 0; ranges[rangeStart] != 0 || ranges[rangeStart + 1] != 0; rangeStart += kEntriesPerRange)
		{
			if (ranges[rangeStart] == codepoint && ranges[rangeStart + 1] == codepoint)
			{
				found = true;
				break;
			}
		}

		CHECK(found);
	}

	int terminatorStart = 0;
	while (ranges[terminatorStart] != 0 || ranges[terminatorStart + 1] != 0)
	{
		terminatorStart += kEntriesPerRange;
	}
	CHECK(ranges[terminatorStart] == 0);
	CHECK(ranges[terminatorStart + 1] == 0);
}

TEST_CASE("UsedGlyphRanges holds exactly the used icons, one single-codepoint range each")
{
	const ImWchar* ranges = UsedGlyphRanges();
	REQUIRE(ranges != nullptr);

	std::size_t rangeCount = 0;
	for (const char* icon : Core::UsedIcons())
	{
		const ImWchar codepoint = CodepointOf(icon);
		const std::size_t rangeStart = kEntriesPerRange * rangeCount;

		CHECK(ranges[rangeStart] == codepoint);
		CHECK(ranges[rangeStart + 1] == codepoint);
		++rangeCount;
	}

	CHECK(rangeCount > 0);
	CHECK(ranges[kEntriesPerRange * rangeCount] == 0);
	CHECK(ranges[kEntriesPerRange * rangeCount + 1] == 0);
}

TEST_CASE("UsedIconSpan is one range from the lowest to the highest used icon, zero-terminated")
{
	const ImWchar* span = Core::UsedIconSpan();
	REQUIRE(span != nullptr);

	ImWchar lowest = CodepointOf(Core::UsedIcons().front());
	ImWchar highest = lowest;
	for (const char* icon : Core::UsedIcons())
	{
		const ImWchar codepoint = CodepointOf(icon);
		lowest = std::min(lowest, codepoint);
		highest = std::max(highest, codepoint);
	}

	CHECK(span[0] == lowest);
	CHECK(span[1] == highest);
	CHECK(span[2] == 0);
}
