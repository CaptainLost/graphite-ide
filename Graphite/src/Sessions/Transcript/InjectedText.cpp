#include "Sessions/Transcript/InjectedText.h"

#include "Core/Base/Strings.h"
#include "Core/FileSystem/FileSystem.h"

#include "Sessions/SessionSummary.h"
#include "UI/TypographicMarks.h"

#include <array>

namespace Graphite {

	namespace {
		struct Marker
		{
			std::string_view OpeningText;
			std::string_view Label;
		};

		constexpr std::array kMarkers{
			Marker{"<task-notification>", "Task notification"},
			Marker{"<command-name>", "Command"},
			Marker{"<command-message>", "Command"},
			Marker{"<local-command-stdout>", "Command output"},
			Marker{"<system-reminder>", "System reminder"},
			Marker{"<user-prompt-submit-hook>", "Hook output"},
			Marker{"[Request interrupted by user", "Interrupted by user"},
		};

		constexpr std::string_view kWhitespace = " \t\r\n";
		constexpr std::string_view kImageOpening = "[Image: source: ";

		std::string_view WithoutLeadingWhitespace(std::string_view text)
		{
			const std::size_t firstNonWhitespace = text.find_first_not_of(kWhitespace);
			return firstNonWhitespace == std::string_view::npos ? std::string_view{} : text.substr(firstNonWhitespace);
		}

		const Marker* MarkerOf(std::string_view text)
		{
			const std::string_view trimmed = WithoutLeadingWhitespace(text);

			for (const Marker& marker : kMarkers)
			{
				if (trimmed.starts_with(marker.OpeningText))
				{
					return &marker;
				}
			}

			return nullptr;
		}

		std::string_view TextBetweenTags(std::string_view text, std::string_view tagName)
		{
			const std::string openingTag = "<" + std::string{tagName} + ">";
			const std::string closingTag = "</" + std::string{tagName} + ">";

			const std::size_t openingTagStart = text.find(openingTag);
			if (openingTagStart == std::string_view::npos)
			{
				return {};
			}

			const std::size_t contentStart = openingTagStart + openingTag.size();
			const std::size_t contentEnd = text.find(closingTag, contentStart);
			return contentEnd == std::string_view::npos
					   ? std::string_view{}
					   : WithoutLeadingWhitespace(text.substr(contentStart, contentEnd - contentStart));
		}
	}

	bool IsInjectedText(std::string_view text)
	{
		return MarkerOf(text) != nullptr;
	}

	std::string InjectedLabel(std::string_view text)
	{
		const Marker* marker = MarkerOf(text);

		if (marker == nullptr)
		{
			return "Injected text";
		}

		if (marker->OpeningText == "<task-notification>")
		{
			const std::string_view summary = TextBetweenTags(text, "summary");
			return summary.empty() ? std::string{marker->Label} : std::string{summary};
		}

		if (marker->OpeningText == "<command-name>")
		{
			const std::string_view name = TextBetweenTags(text, "command-name");
			return name.empty() ? std::string{marker->Label} : "Command " + std::string{name};
		}

		return std::string{marker->Label};
	}

	std::string FirstLineLabel(std::string_view text, std::size_t maxBytes)
	{
		const std::string_view line = Core::FirstLine(text);
		std::string label;

		if (line.starts_with(kImageOpening) && line.ends_with(']'))
		{
			const std::string_view path = line.substr(kImageOpening.size(), line.size() - kImageOpening.size() - 1);
			const std::string_view name = Core::AfterLastPathSeparator(path);

			if (!name.empty())
			{
				label = "Image source: " + std::string{name};
			}
		}

		if (label.empty())
		{
			label = line;
		}

		if (label.size() <= maxBytes)
		{
			return label;
		}

		const std::size_t bytesBeforeEllipsis = maxBytes > kEllipsis.size() ? maxBytes - kEllipsis.size() : 0;
		return TruncateUtf8(label, bytesBeforeEllipsis) + std::string{kEllipsis};
	}
}
