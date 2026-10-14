#pragma once

#include "Settings/Panel/CommandControls.h"
#include "Settings/Panel/SettingsPage.h"

#include <array>
#include <string_view>
#include <vector>

namespace Core {

	class CommandRegistry;
}

namespace Graphite {

	inline const std::array<CommandChoice, 3> kIgnoredFileChoices{CommandChoice{.Label = "Show", .CommandId = "git.ignored.show"},
																  CommandChoice{.Label = "Dim", .CommandId = "git.ignored.dim"},
																  CommandChoice{.Label = "Hide", .CommandId = "git.ignored.hide"}};

	class ExplorerPage final : public SettingsPage
	{
	public:
		explicit ExplorerPage(Core::CommandRegistry& commands) : m_Commands(commands) {}

		std::string_view GetId() const override { return "explorer"; }

		std::string_view GetTitle() const override { return "Explorer"; }

		const char* GetIcon() const override;

		std::vector<SettingsCard> Describe() override;

	private:
		void DrawFollowActiveFile();
		void DrawIgnoredFiles();

		Core::CommandRegistry& m_Commands;
	};
}
