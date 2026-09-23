#include "corepch.h"

#include "Core/ImGui/Icons.h"

#include <IconsMaterialDesignIcons.h>

#include <cctype>

namespace Core {

	namespace {
		// Every icon used here sits above U+FFFF, which is always a 4-byte UTF-8 sequence.
		// Decoded by hand because ImGui's own decoder is not part of its public API.
		ImWchar DecodeIcon(const char* utf8)
		{
			const auto b0 = static_cast<unsigned char>(utf8[0]);
			const auto b1 = static_cast<unsigned char>(utf8[1]);
			const auto b2 = static_cast<unsigned char>(utf8[2]);
			const auto b3 = static_cast<unsigned char>(utf8[3]);
			return static_cast<ImWchar>(((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) |
										((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu));
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
		};

		constexpr std::size_t kUsedIconCount = sizeof(kUsedIcons) / sizeof(kUsedIcons[0]);

		std::string ToLowerWithDot(std::string_view extension)
		{
			std::string lower{extension};
			std::transform(lower.begin(), lower.end(), lower.begin(),
						   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			if (!lower.empty() && lower.front() != '.')
				lower.insert(lower.begin(), '.');

			return lower;
		}
	}

	const char* IconForExtension(std::string_view extension)
	{
		const std::string lower = ToLowerWithDot(extension);

		// A project extension names the build tool, not what is compiled inside it.
		if (lower == ".vcxproj" || lower == ".csproj")
			return ICON_MDI_APPLICATION_BRACES;

		if (lower == ".c")
			return ICON_MDI_LANGUAGE_C;
		if (lower == ".cpp" || lower == ".cc" || lower == ".cxx" || lower == ".h" ||
			lower == ".hpp" || lower == ".hxx")
			return ICON_MDI_LANGUAGE_CPP;
		if (lower == ".cs")
			return ICON_MDI_LANGUAGE_CSHARP;
		if (lower == ".lua")
			return ICON_MDI_LANGUAGE_LUA;
		if (lower == ".json")
			return ICON_MDI_CODE_JSON;
		if (lower == ".md")
			return ICON_MDI_LANGUAGE_MARKDOWN;
		if (lower == ".py")
			return ICON_MDI_LANGUAGE_PYTHON;

		return ICON_MDI_FILE;
	}

	const ImWchar* UsedGlyphRanges()
	{
		static ImWchar ranges[2 * kUsedIconCount + 2] = {};
		static bool built = false;

		if (!built)
		{
			for (std::size_t i = 0; i < kUsedIconCount; ++i)
			{
				const ImWchar codepoint = DecodeIcon(kUsedIcons[i]);
				ranges[2 * i] = codepoint;
				ranges[2 * i + 1] = codepoint;
			}

			built = true;
		}

		return ranges;
	}
}