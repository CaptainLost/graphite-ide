#include "Sessions/InjectedText.h"

#include <array>

namespace Graphite {

	namespace {
		struct Marker
		{
			std::string_view Tag;
			std::string_view Label;
		};

		constexpr std::array kMarkers{
			Marker{"<task-notification>", "Task notification"},
			Marker{"<command-name>", "Command"},
			Marker{"<command-message>", "Command"},
			Marker{"<local-command-stdout>", "Command output"},
			Marker{"<system-reminder>", "System reminder"},
			Marker{"<user-prompt-submit-hook>", "Hook output"},
		};

		std::string_view Trimmed(std::string_view text)
		{
			const std::size_t start = text.find_first_not_of(" \t\r\n");
			return start == std::string_view::npos ? std::string_view{} : text.substr(start);
		}

		const Marker* MarkerOf(std::string_view text)
		{
			const std::string_view trimmed = Trimmed(text);

			for (const Marker& marker : kMarkers)
			{
				if (trimmed.starts_with(marker.Tag))
					return &marker;
			}

			return nullptr;
		}

		// The text between <tag> and </tag>, or nothing when either is missing.
		std::string_view Between(std::string_view text, std::string_view tag)
		{
			const std::string open = "<" + std::string{tag} + ">";
			const std::string close = "</" + std::string{tag} + ">";

			const std::size_t start = text.find(open);
			if (start == std::string_view::npos)
				return {};

			const std::size_t from = start + open.size();
			const std::size_t end = text.find(close, from);
			return end == std::string_view::npos ? std::string_view{} : Trimmed(text.substr(from, end - from));
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
			return "Injected text";

		if (marker->Tag == "<task-notification>")
		{
			const std::string_view summary = Between(text, "summary");
			return summary.empty() ? std::string{marker->Label} : std::string{summary};
		}

		if (marker->Tag == "<command-name>")
		{
			const std::string_view name = Between(text, "command-name");
			return name.empty() ? std::string{marker->Label} : "Command " + std::string{name};
		}

		return std::string{marker->Label};
	}
}
