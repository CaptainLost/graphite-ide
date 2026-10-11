#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	class CommandRegistry;
}

namespace Graphite {

	struct CommandChoice
	{
		std::string Label;
		std::string CommandId;
	};

	// Drawn from the command that owns the setting, so this toggle and the View menu's tick cannot disagree.
	void DrawCommandToggle(Core::CommandRegistry& commands, std::string_view commandId);

	// The number of choices when no choice's command says it is checked.
	std::size_t CheckedChoice(const Core::CommandRegistry& commands, std::span<const CommandChoice> choices);

	void RunChoice(Core::CommandRegistry& commands, const CommandChoice& choice);

	void DrawCommandChoice(Core::CommandRegistry& commands, const char* id, std::span<const CommandChoice> choices);

	std::vector<std::string> LabelsOf(std::span<const CommandChoice> choices);
}
