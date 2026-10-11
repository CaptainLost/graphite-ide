#include "Settings/Panel/CommandControls.h"

#include "Settings/Panel/SettingsLayout.h"

#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/ImGui/Toggle.h"

namespace Graphite {

	namespace {

		bool IsChecked(const Core::Command& command)
		{
			return command.Checked && command.Checked();
		}
	}

	void DrawCommandToggle(Core::CommandRegistry& commands, std::string_view commandId)
	{
		const Core::Command* command = commands.Find(commandId);

		if (command == nullptr)
		{
			return;
		}

		bool value = IsChecked(*command);
		const std::string toggleId = "##" + std::string{commandId};

		if (Core::Toggle(toggleId.c_str(), &value) && command->Run)
		{
			command->Run();
		}
	}

	std::size_t CheckedChoice(const Core::CommandRegistry& commands, std::span<const CommandChoice> choices)
	{
		for (std::size_t choiceIndex = 0; choiceIndex < choices.size(); ++choiceIndex)
		{
			const Core::Command* command = commands.Find(choices[choiceIndex].CommandId);

			if (command != nullptr && IsChecked(*command))
			{
				return choiceIndex;
			}
		}

		return choices.size();
	}

	void RunChoice(Core::CommandRegistry& commands, const CommandChoice& choice)
	{
		const Core::Command* command = commands.Find(choice.CommandId);

		if (command != nullptr && command->Run)
		{
			command->Run();
		}
	}

	void DrawCommandChoice(Core::CommandRegistry& commands, const char* id, std::span<const CommandChoice> choices)
	{
		const std::vector<std::string> labels = LabelsOf(choices);
		std::size_t selected = CheckedChoice(commands, choices);

		if (SettingsLayout::Choice(id, labels, selected))
		{
			RunChoice(commands, choices[selected]);
		}
	}

	std::vector<std::string> LabelsOf(std::span<const CommandChoice> choices)
	{
		std::vector<std::string> labels;

		for (const CommandChoice& choice : choices)
		{
			labels.push_back(choice.Label);
		}

		return labels;
	}
}
