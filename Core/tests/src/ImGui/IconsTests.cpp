#include <doctest/doctest.h>

#include "Core/ImGui/Icons.h"

#include <IconsMaterialDesignIcons.h>

#include <cstring>

using Core::IconForExtension;
using Core::UsedGlyphRanges;

TEST_CASE("IconForExtension recognizes known source extensions")
{
	CHECK(std::strcmp(IconForExtension(".c"), ICON_MDI_LANGUAGE_C) == 0);
	CHECK(std::strcmp(IconForExtension(".C"), ICON_MDI_LANGUAGE_C) == 0);
	CHECK(std::strcmp(IconForExtension(".cpp"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension(".CPP"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension(".cc"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension(".cxx"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension(".h"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension(".hpp"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension(".hxx"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension(".cs"), ICON_MDI_LANGUAGE_CSHARP) == 0);
	CHECK(std::strcmp(IconForExtension(".lua"), ICON_MDI_LANGUAGE_LUA) == 0);
	CHECK(std::strcmp(IconForExtension(".json"), ICON_MDI_CODE_JSON) == 0);
	CHECK(std::strcmp(IconForExtension(".md"), ICON_MDI_LANGUAGE_MARKDOWN) == 0);
	CHECK(std::strcmp(IconForExtension(".py"), ICON_MDI_LANGUAGE_PYTHON) == 0);
}

TEST_CASE("IconForExtension tells a C# project from a C++ one")
{
	CHECK(std::strcmp(IconForExtension(".vcxproj"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension(".csproj"), ICON_MDI_LANGUAGE_CSHARP) == 0);
	CHECK(std::strcmp(IconForExtension(".vcxproj"), IconForExtension(".csproj")) != 0);
}

TEST_CASE("IconForExtension falls back to a generic file icon")
{
	CHECK(std::strcmp(IconForExtension(".xyz"), ICON_MDI_FILE) == 0);
	CHECK(std::strcmp(IconForExtension(""), ICON_MDI_FILE) == 0);
}

TEST_CASE("IconForExtension accepts an extension with or without the leading dot")
{
	CHECK(std::strcmp(IconForExtension("cpp"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension("cpp"), IconForExtension(".cpp")) == 0);
}

TEST_CASE("UsedGlyphRanges covers every icon this app can display, zero-terminated")
{
	const ImWchar* ranges = UsedGlyphRanges();
	REQUIRE(ranges != nullptr);

	// Decode each icon's UTF-8 by hand (every MDI glyph used here is a 4-byte sequence,
	// since they all sit above U+FFFF) and check it appears as a [c, c] pair.
	const char* icons[] = {
		ICON_MDI_FILE,
		ICON_MDI_FOLDER,
		ICON_MDI_FOLDER_OPEN,
		ICON_MDI_LANGUAGE_C,
		ICON_MDI_LANGUAGE_CPP,
		ICON_MDI_LANGUAGE_CSHARP,
		ICON_MDI_LANGUAGE_LUA,
		ICON_MDI_CODE_JSON,
		ICON_MDI_LANGUAGE_MARKDOWN,
		ICON_MDI_LANGUAGE_PYTHON,
		ICON_MDI_APPLICATION_BRACES,
		ICON_MDI_REFRESH,
		ICON_MDI_FILE_TREE,
		ICON_MDI_FILE_DOCUMENT_EDIT_OUTLINE,
		ICON_MDI_SOURCE_BRANCH,
		ICON_MDI_MESSAGE_TEXT_OUTLINE,
		ICON_MDI_CONSOLE,
		ICON_MDI_ALERT_CIRCLE_OUTLINE,
		ICON_MDI_LINK_VARIANT,
		ICON_MDI_TEXT_SEARCH,
		ICON_MDI_COG_OUTLINE,
		ICON_MDI_ROBOT_OUTLINE,
		ICON_MDI_FILE_DOCUMENT_OUTLINE,
		ICON_MDI_PENCIL,
		ICON_MDI_WEB,
		ICON_MDI_TOOLS,
		ICON_MDI_BELL_OUTLINE,
		ICON_MDI_TIMER_SAND,
		ICON_MDI_STOP_CIRCLE_OUTLINE,
		ICON_MDI_WRENCH_OUTLINE,
		ICON_MDI_HELP_CIRCLE_OUTLINE,
		ICON_MDI_CACHED,
		ICON_MDI_ACCOUNT_OUTLINE,
		ICON_MDI_PUZZLE_OUTLINE,
		ICON_MDI_SERVER_NETWORK,
		ICON_MDI_BRAIN,
		ICON_MDI_ARROW_RIGHT,
		ICON_MDI_IMAGE_OUTLINE,
		ICON_MDI_ARROW_UP,
		ICON_MDI_ARROW_DOWN,
		ICON_MDI_CIRCLE_MEDIUM,
		ICON_MDI_ARCHIVE_ARROW_DOWN_OUTLINE,
	};

	for (const char* icon : icons)
	{
		const auto b0 = static_cast<unsigned char>(icon[0]);
		const auto b1 = static_cast<unsigned char>(icon[1]);
		const auto b2 = static_cast<unsigned char>(icon[2]);
		const auto b3 = static_cast<unsigned char>(icon[3]);
		const auto codepoint = static_cast<ImWchar>(((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) |
													((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu));

		bool found = false;
		for (int i = 0; ranges[i] != 0 || ranges[i + 1] != 0; i += 2)
		{
			if (ranges[i] == codepoint && ranges[i + 1] == codepoint)
			{
				found = true;
				break;
			}
		}

		CHECK(found);
	}

	int count = 0;
	while (ranges[count] != 0 || ranges[count + 1] != 0)
	{
		count += 2;
	}
	CHECK(ranges[count] == 0);
	CHECK(ranges[count + 1] == 0);
}
