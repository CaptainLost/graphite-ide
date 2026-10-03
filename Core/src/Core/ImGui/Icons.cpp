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
		// Decoded here because ImGui's own decoder is not part of its public API. The icon
		// strings are the font header's own constants, so one that is not UTF-8 is a broken
		// header: asserted, and baked as U+FFFD rather than as a range that ends the list.
		ImWchar DecodeIcon(const char* utf8)
		{
			std::size_t offset = 0;
			const std::optional<char32_t> codePoint = DecodeUtf8(utf8, offset);
			CORE_ASSERT(codePoint.has_value(), "An icon constant is not valid UTF-8");
			return static_cast<ImWchar>(codePoint.value_or(kReplacementCharacter));
		}

		// The one list UsedGlyphRanges bakes from: an icon added here is baked, and one that
		// is not here has no glyph to draw.
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
		};

		constexpr std::size_t kUsedIconCount = sizeof(kUsedIcons) / sizeof(kUsedIcons[0]);

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
		const std::string lower = ToLowerWithDot(extension);

		// A project extension names the build tool, not what is compiled inside it.
		if (lower == ".vcxproj" || lower == ".csproj")
		{
			return ICON_MDI_APPLICATION_BRACES;
		}

		if (lower == ".c")
		{
			return ICON_MDI_LANGUAGE_C;
		}
		if (lower == ".cpp" || lower == ".cc" || lower == ".cxx" || lower == ".h" ||
			lower == ".hpp" || lower == ".hxx")
		{
			return ICON_MDI_LANGUAGE_CPP;
		}
		if (lower == ".cs")
		{
			return ICON_MDI_LANGUAGE_CSHARP;
		}
		if (lower == ".lua")
		{
			return ICON_MDI_LANGUAGE_LUA;
		}
		if (lower == ".json")
		{
			return ICON_MDI_CODE_JSON;
		}
		if (lower == ".md")
		{
			return ICON_MDI_LANGUAGE_MARKDOWN;
		}
		if (lower == ".py")
		{
			return ICON_MDI_LANGUAGE_PYTHON;
		}

		return ICON_MDI_FILE;
	}

	const ImWchar* UsedGlyphRanges()
	{
		// Built once by the static's own initialiser, which the language makes thread-safe, and
		// zero-terminated by the two entries the loop leaves alone.
		static const std::array<ImWchar, 2 * kUsedIconCount + 2> ranges = [] {
			std::array<ImWchar, 2 * kUsedIconCount + 2> built{};

			for (std::size_t i = 0; i < kUsedIconCount; ++i)
			{
				const ImWchar codepoint = DecodeIcon(kUsedIcons[i]);
				built[2 * i] = codepoint;
				built[2 * i + 1] = codepoint;
			}

			return built;
		}();

		return ranges.data();
	}
}