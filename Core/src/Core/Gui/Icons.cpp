#include "corepch.h"

#include "Core/Gui/Icons.h"

#include <IconsMaterialDesignIcons.h>

#include <cctype>

namespace core
{
    namespace
    {
        // Every icon this app uses sits in MDI's private-use plane (above U+FFFF), which
        // is always a 4-byte UTF-8 sequence — decoding it by hand avoids depending on an
        // internal ImGui helper that isn't part of its public API.
        ImWchar DecodeIcon(const char* utf8)
        {
            const auto b0 = static_cast<unsigned char>(utf8[0]);
            const auto b1 = static_cast<unsigned char>(utf8[1]);
            const auto b2 = static_cast<unsigned char>(utf8[2]);
            const auto b3 = static_cast<unsigned char>(utf8[3]);
            return static_cast<ImWchar>(((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) |
                                          ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu));
        }

        // Kept in one array so UsedGlyphRanges() can never drift from what
        // IconForExtension() and the panels actually return/use — add a new icon here
        // first and the range that bakes it follows automatically.
        constexpr const char* kUsedIcons[] = {
            ICON_MDI_FILE,
            ICON_MDI_FOLDER,
            ICON_MDI_FOLDER_OPEN,
            ICON_MDI_LANGUAGE_CPP,
            ICON_MDI_LANGUAGE_CSHARP,
            ICON_MDI_LANGUAGE_LUA,
            ICON_MDI_CODE_JSON,
            ICON_MDI_LANGUAGE_MARKDOWN,
            ICON_MDI_LANGUAGE_PYTHON,
            ICON_MDI_REFRESH,
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

        if (lower == ".cpp" || lower == ".cc" || lower == ".cxx" || lower == ".h" ||
            lower == ".hpp" || lower == ".hxx" || lower == ".vcxproj")
            return ICON_MDI_LANGUAGE_CPP;
        if (lower == ".cs" || lower == ".csproj")
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
