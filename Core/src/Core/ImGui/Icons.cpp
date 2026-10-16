#include "corepch.h"

#include "Core/ImGui/Icons.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Assert.h"
#include "Core/Text/Utf8.h"

#include <IconsMaterialDesignIcons.h>

#include <array>
#include <optional>

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

		constexpr const char* kUsedIcons[] = {
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
			ICON_MDI_MAGNIFY,
			ICON_MDI_WINDOW_MINIMIZE,
			ICON_MDI_WINDOW_MAXIMIZE,
			ICON_MDI_WINDOW_RESTORE,
			ICON_MDI_CLOSE,
			ICON_MDI_CHEVRON_UP,
			ICON_MDI_CHEVRON_DOWN,
			ICON_MDI_CHEVRON_RIGHT,
			ICON_MDI_FORMAT_LETTER_CASE,
			ICON_MDI_FORMAT_LETTER_MATCHES,
			ICON_MDI_PLUS,
			ICON_MDI_MINUS,
			ICON_MDI_UNDO,
			ICON_MDI_DELETE,
			ICON_MDI_CHECK,
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
			ICON_MDI_VIEW_SPLIT_VERTICAL,
			ICON_MDI_VIEW_AGENDA_OUTLINE,
			ICON_MDI_ARROW_COLLAPSE_VERTICAL,
			ICON_MDI_ARROW_EXPAND_VERTICAL,
			ICON_MDI_PALETTE_OUTLINE,
			ICON_MDI_FILE_EDIT_OUTLINE,
			ICON_MDI_TAB_PLUS,
			ICON_MDI_ARROW_UP,
			ICON_MDI_ARROW_DOWN,
			ICON_MDI_CIRCLE_MEDIUM,
			ICON_MDI_ARCHIVE_ARROW_DOWN_OUTLINE,
			ICON_MDI_ALERT,
			ICON_MDI_ARROW_LEFT,
			ICON_MDI_CHECK_ALL,
			ICON_MDI_CLOSE_CIRCLE,
			ICON_MDI_CODE_BRACES,
			ICON_MDI_DELETE_SWEEP_OUTLINE,
			ICON_MDI_EYE_OFF,
			ICON_MDI_FILE_OUTLINE,
			ICON_MDI_FORMAT_LIST_BULLETED,
			ICON_MDI_INFORMATION_OUTLINE,
			ICON_MDI_KEYBOARD_OUTLINE,
			ICON_MDI_MICROSOFT_VISUAL_STUDIO,
			ICON_MDI_PENCIL_OUTLINE,
			ICON_MDI_PROGRESS_CLOCK,
			ICON_MDI_RADIOBOX_MARKED,
			ICON_MDI_RESTORE,
			ICON_MDI_SERVER_OUTLINE,
			ICON_MDI_SHIELD_ALERT_OUTLINE,
			ICON_MDI_SHIELD_CHECK_OUTLINE,
			ICON_MDI_SOURCE_PULL,
			ICON_MDI_TEXT_BOX_OUTLINE,
			ICON_MDI_TUNE_VARIANT,
			ICON_MDI_WRAP,
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

		bool IsCppExtension(const std::string& dottedExtension)
		{
			return dottedExtension == ".cpp" || dottedExtension == ".cc" ||
				   dottedExtension == ".cxx" || dottedExtension == ".h" ||
				   dottedExtension == ".hpp" || dottedExtension == ".hxx";
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
			return ICON_MDI_LANGUAGE_CPP;
		}
		if (dottedExtension == ".csproj")
		{
			return ICON_MDI_LANGUAGE_CSHARP;
		}

		if (dottedExtension == ".c")
		{
			return ICON_MDI_LANGUAGE_C;
		}
		if (IsCppExtension(dottedExtension))
		{
			return ICON_MDI_LANGUAGE_CPP;
		}
		if (dottedExtension == ".cs")
		{
			return ICON_MDI_LANGUAGE_CSHARP;
		}
		if (dottedExtension == ".lua")
		{
			return ICON_MDI_LANGUAGE_LUA;
		}
		if (dottedExtension == ".json")
		{
			return ICON_MDI_CODE_JSON;
		}
		if (dottedExtension == ".md")
		{
			return ICON_MDI_LANGUAGE_MARKDOWN;
		}
		if (dottedExtension == ".py")
		{
			return ICON_MDI_LANGUAGE_PYTHON;
		}

		return ICON_MDI_FILE;
	}

	// WARNING: ImGui keeps the pointer for the atlas's lifetime, so the ranges live in static storage.
	const ImWchar* UsedGlyphRanges()
	{
		static const GlyphRanges ranges = BuildGlyphRanges();

		return ranges.data();
	}
}
