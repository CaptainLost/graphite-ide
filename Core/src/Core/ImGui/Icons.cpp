#include "corepch.h"

#include "Core/ImGui/Icons.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Assert.h"
#include "Core/Text/Utf8.h"

#include <IconsLucide.h>

#include <algorithm>
#include <array>
#include <optional>
#include <span>

namespace Core {

	namespace {
		// WARNING: decoded here because ImGui's own decoder is not part of its public API. An icon that is not UTF-8 is baked as U+FFFD, since a zero would end the list of ranges.
		ImWchar DecodeIcon(const char* iconUtf8)
		{
			std::size_t offset = 0;
			const std::optional<char32_t> codePoint = DecodeUtf8(iconUtf8, offset);
			CORE_ASSERT(codePoint.has_value(), "An icon constant is not valid UTF-8");
			return static_cast<ImWchar>(codePoint.value_or(kReplacementCharacter));
		}

		// WARNING: only these icons are baked from the icon font; any other draws ImGui's fallback glyph.
		constexpr const char* kUsedIcons[] = {
			ICON_LC_FILE,
			ICON_LC_FOLDER,
			ICON_LC_FOLDER_OPEN,
			ICON_LC_FILE_CODE,
			ICON_LC_HASH,
			ICON_LC_FILE_JSON,
			ICON_LC_FILE_TEXT,
			ICON_LC_APP_WINDOW,
			ICON_LC_REFRESH_CW,
			ICON_LC_SEARCH,
			ICON_LC_MINUS,
			ICON_LC_SQUARE,
			ICON_LC_COPY,
			ICON_LC_X,
			ICON_LC_CHEVRON_UP,
			ICON_LC_CHEVRON_DOWN,
			ICON_LC_MAXIMIZE,
			ICON_LC_CHEVRON_RIGHT,
			ICON_LC_CASE_SENSITIVE,
			ICON_LC_WHOLE_WORD,
			ICON_LC_PLUS,
			ICON_LC_UNDO_2,
			ICON_LC_TRASH_2,
			ICON_LC_CHECK,
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
			ICON_LC_COLUMNS_2,
			ICON_LC_ROWS_2,
			ICON_LC_CHEVRONS_DOWN_UP,
			ICON_LC_CHEVRONS_UP_DOWN,
			ICON_LC_PALETTE,
			ICON_LC_FILE_PEN,
			ICON_LC_SQUARE_PLUS,
			ICON_LC_ARROW_UP,
			ICON_LC_ARROW_DOWN,
			ICON_LC_ARCHIVE,
			ICON_LC_TRIANGLE_ALERT,
			ICON_LC_ARROW_LEFT,
			ICON_LC_CHECK_CHECK,
			ICON_LC_CIRCLE_X,
			ICON_LC_BRACES,
			ICON_LC_LIST_X,
			ICON_LC_EYE_OFF,
			ICON_LC_LIST,
			ICON_LC_INFO,
			ICON_LC_KEYBOARD,
			ICON_LC_CLOCK,
			ICON_LC_CIRCLE_DOT,
			ICON_LC_ROTATE_CCW,
			ICON_LC_SERVER,
			ICON_LC_SHIELD_ALERT,
			ICON_LC_SHIELD_CHECK,
			ICON_LC_GIT_PULL_REQUEST,
			ICON_LC_SCROLL_TEXT,
			ICON_LC_SLIDERS_HORIZONTAL,
			ICON_LC_WRAP_TEXT,
			ICON_LC_FILE_CODE_2,
			ICON_LC_TERMINAL,
			ICON_LC_FILE_IMAGE,
			ICON_LC_FILE_PLUS,
			ICON_LC_FOLDER_PLUS,
			ICON_LC_HISTORY,
		};

		constexpr std::size_t kUsedIconCount = sizeof(kUsedIcons) / sizeof(kUsedIcons[0]);

		constexpr std::size_t kEntriesPerRange = 2;
		constexpr std::size_t kTerminatorEntryCount = 2;
		constexpr std::size_t kGlyphRangeEntryCount = kEntriesPerRange * kUsedIconCount + kTerminatorEntryCount;

		using GlyphRanges = std::array<ImWchar, kGlyphRangeEntryCount>;

		GlyphRanges BuildGlyphRanges()
		{
			GlyphRanges ranges{};

			for (std::size_t iconIndex = 0; iconIndex < kUsedIconCount; ++iconIndex)
			{
				const ImWchar codepoint = DecodeIcon(kUsedIcons[iconIndex]);
				const std::size_t rangeStart = kEntriesPerRange * iconIndex;
				ranges[rangeStart] = codepoint;
				ranges[rangeStart + 1] = codepoint;
			}

			return ranges;
		}

		constexpr std::size_t kSpanTerminatorEntryCount = 1;

		using IconSpan = std::array<ImWchar, kEntriesPerRange + kSpanTerminatorEntryCount>;

		IconSpan BuildUsedIconSpan()
		{
			ImWchar lowest = DecodeIcon(kUsedIcons[0]);
			ImWchar highest = lowest;

			for (const char* icon : kUsedIcons)
			{
				const ImWchar codepoint = DecodeIcon(icon);
				lowest = std::min(lowest, codepoint);
				highest = std::max(highest, codepoint);
			}

			return {lowest, highest, 0};
		}

		bool IsCppSourceExtension(const std::string& dottedExtension)
		{
			return dottedExtension == ".cpp" || dottedExtension == ".cc" || dottedExtension == ".cxx";
		}

		bool IsCppHeaderExtension(const std::string& dottedExtension)
		{
			return dottedExtension == ".h" || dottedExtension == ".hpp" || dottedExtension == ".hxx";
		}

		std::string ToLowerWithDot(std::string_view extension)
		{
			std::string lower = Lowered(extension);

			if (!lower.empty() && lower.front() != '.')
			{
				lower.insert(lower.begin(), '.');
			}

			return lower;
		}
	}

	const char* IconForExtension(std::string_view extension)
	{
		const std::string dottedExtension = ToLowerWithDot(extension);

		if (dottedExtension == ".vcxproj")
		{
			return ICON_LC_FILE_CODE;
		}
		if (dottedExtension == ".csproj")
		{
			return ICON_LC_HASH;
		}

		if (dottedExtension == ".c")
		{
			return ICON_LC_FILE_CODE;
		}
		if (IsCppSourceExtension(dottedExtension))
		{
			return ICON_LC_FILE_CODE;
		}
		if (IsCppHeaderExtension(dottedExtension))
		{
			return ICON_LC_FILE_CODE_2;
		}
		if (dottedExtension == ".cs")
		{
			return ICON_LC_HASH;
		}
		if (dottedExtension == ".lua")
		{
			return ICON_LC_FILE_CODE;
		}
		if (dottedExtension == ".json")
		{
			return ICON_LC_FILE_JSON;
		}
		if (dottedExtension == ".md")
		{
			return ICON_LC_FILE_TEXT;
		}
		if (dottedExtension == ".py")
		{
			return ICON_LC_FILE_CODE;
		}

		return ICON_LC_FILE;
	}

	// WARNING: ImGui keeps the pointer for the atlas's lifetime, so the ranges live in static storage.
	const ImWchar* UsedGlyphRanges()
	{
		static const GlyphRanges ranges = BuildGlyphRanges();

		return ranges.data();
	}

	const ImWchar* UsedIconSpan()
	{
		static const IconSpan span = BuildUsedIconSpan();

		return span.data();
	}

	std::span<const char* const> UsedIcons()
	{
		return kUsedIcons;
	}
}
