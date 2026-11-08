#include "Git/Network/NetworkCommands.h"

#include "Core/Command/CommandRegistry.h"
#include "Git/GitModel.h"
#include "Git/Network/NetworkModel.h"
#include "Git/Network/NetworkText.h"

#include <chrono>
#include <utility>

namespace Graphite {

	namespace {

		NetworkCommandState StateOf(const ButtonText& button)
		{
			const bool starts = button.Enabled && !button.Running;
			return NetworkCommandState{.Enabled = starts, .Note = starts ? std::string{} : button.Tooltip};
		}
	}

	std::string ChooseRemoteFromBarText()
	{
		return "Choose the remote from Push in the Git window.";
	}

	NetworkCommandState CommandStateOfFetch(const NetworkButtons& buttons)
	{
		return StateOf(buttons.Fetch);
	}

	NetworkCommandState CommandStateOfPull(const NetworkButtons& buttons)
	{
		return StateOf(buttons.Pull);
	}

	NetworkCommandState CommandStateOfPush(const NetworkButtons& buttons)
	{
		if (buttons.Push.AsksWhichRemote && buttons.Push.Enabled)
		{
			return NetworkCommandState{.Enabled = false, .Note = ChooseRemoteFromBarText()};
		}

		return StateOf(buttons.Push);
	}

	NetworkCommandState CommandStateOfFullHistory(const NetworkButtons& buttons, const NetworkState& state)
	{
		if (state.Running.has_value())
		{
			return NetworkCommandState{.Enabled = false, .Note = RunningText(state)};
		}

		return StateOf(buttons.Fetch);
	}

	NetworkButtons DescribeNetworkButtonsNow(const GitModel& git, const NetworkModel& network, bool checkingOut, bool folderTrusted)
	{
		const Core::Ref<const Core::HeadState> head = git.GetHead();
		const Core::Ref<const std::vector<Core::Branch>> branches = git.GetBranches();
		const Core::Ref<const std::vector<Core::Remote>> remotes = git.GetRemotes();
		const std::chrono::sys_seconds now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());

		return DescribeNetworkButtons(WindowBarInput{git.GetWorkingDirectory(), head.get(), branches.get(), remotes.get(), checkingOut},
									  network.GetState(), network.ShownLastFetch(git.GetLastFetch()), now,
									  NetworkAccess{.GitFound = git.IsGitFound(), .FolderTrusted = folderTrusted});
	}

	void RegisterNetworkCommands(Core::CommandRegistry& commands, NetworkCommandServices services)
	{
		// WARNING: The state is read from the buttons each time, never kept, so a menu opened mid-operation shows it.
		commands.Add({.Id = "git.fetch",
					  .Label = "Git: Fetch",
					  .Run = services.Fetch,
					  .Enabled = [buttons = services.Buttons] { return CommandStateOfFetch(buttons()).Enabled; }});

		commands.Add({.Id = "git.pull",
					  .Label = "Git: Pull",
					  .Run = services.Pull,
					  .Enabled = [buttons = services.Buttons] { return CommandStateOfPull(buttons()).Enabled; }});

		commands.Add({.Id = "git.push",
					  .Label = "Git: Push",
					  .Run = services.Push,
					  .Enabled = [buttons = services.Buttons] { return CommandStateOfPush(buttons()).Enabled; },
					  .DisabledNote = ChooseRemoteFromBarText()});
	}
}
