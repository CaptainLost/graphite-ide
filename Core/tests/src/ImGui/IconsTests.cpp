#include <doctest/doctest.h>

#include "Core/ImGui/Icons.h"

#include <IconsMaterialDesignIcons.h>

#include <cstring>

using Core::IconForExtension;
using Core::UsedGlyphRanges;

namespace {
	constexpr unsigned int kFourByteLeadMask = 0x07u;
	constexpr unsigned int kContinuationMask = 0x3Fu;
	constexpr unsigned int kLeadShift = 18;
	constexpr unsigned int kSecondShift = 12;
	constexpr unsigned int kThirdShift = 6;
	constexpr int kEntriesPerRange = 2;

	ImWchar DecodeFourByteUtf8(const char* icon)
	{
		const auto firstByte = static_cast<unsigned char>(icon[0]);
		const auto secondByte = static_cast<unsigned char>(icon[1]);
		const auto thirdByte = static_cast<unsigned char>(icon[2]);
		const auto fourthByte = static_cast<unsigned char>(icon[3]);

		return static_cast<ImWchar>(((firstByte & kFourByteLeadMask) << kLeadShift) |
									((secondByte & kContinuationMask) << kSecondShift) |
									((thirdByte & kContinuationMask) << kThirdShift) |
									(fourthByte & kContinuationMask));
	}
}

TEST_CASE("IconForExtension recognizes known source extensions")
{
	CHECK(std::strcmp(IconForExtension(".c"), ICON_MDI_LANGUAGE_C) == 0);
	CHECK(std::strcmp(IconForExtension(".C"), ICON_MDI_LANGUAGE_C) == 0);
	CHECK(std::strcmp(IconForExtension(".cpp"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension(".CPP"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension(".cc"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension(".cxx"), ICON_MDI_LANGUAGE_CPP) == 0);
	CHECK(std::strcmp(IconForExtension(".h"), ICON_MDI_ALPHA_H_BOX_OUTLINE) == 0);
	CHECK(std::strcmp(IconForExtension(".H"), ICON_MDI_ALPHA_H_BOX_OUTLINE) == 0);
	CHECK(std::strcmp(IconForExtension(".hpp"), ICON_MDI_ALPHA_H_BOX_OUTLINE) == 0);
	CHECK(std::strcmp(IconForExtension(".hxx"), ICON_MDI_ALPHA_H_BOX_OUTLINE) == 0);
	CHECK(std::strcmp(IconForExtension(".cs"), ICON_MDI_LANGUAGE_CSHARP) == 0);
	CHECK(std::strcmp(IconForExtension(".lua"), ICON_MDI_LANGUAGE_LUA) == 0);
	CHECK(std::strcmp(IconForExtension(".json"), ICON_MDI_CODE_JSON) == 0);
	CHECK(std::strcmp(IconForExtension(".md"), ICON_MDI_LANGUAGE_MARKDOWN) == 0);
	CHECK(std::strcmp(IconForExtension(".py"), ICON_MDI_LANGUAGE_PYTHON) == 0);
}

TEST_CASE("IconForExtension tells a C++ header from a C++ source")
{
	CHECK(std::strcmp(IconForExtension(".h"), IconForExtension(".cpp")) != 0);
	CHECK(std::strcmp(IconForExtension("h"), IconForExtension(".h")) == 0);
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
		ICON_MDI_ALPHA_H_BOX_OUTLINE,
		ICON_MDI_LANGUAGE_MARKDOWN_OUTLINE,
		ICON_MDI_POWERSHELL,
		ICON_MDI_MICROSOFT_VISUAL_STUDIO,
		ICON_MDI_FILE_IMAGE_OUTLINE,
		ICON_MDI_FILE_OUTLINE,
		ICON_MDI_FOLDER_OUTLINE,
		ICON_MDI_FOLDER_OPEN_OUTLINE,
	};

	for (const char* icon : icons)
	{
		const ImWchar codepoint = DecodeFourByteUtf8(icon);

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
