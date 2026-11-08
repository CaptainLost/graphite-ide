#pragma once

#include "Git/Window/WindowBar.h"

#include <functional>
#include <string>

namespace Core {
	class CommandRegistry;
}

namespace Graphite {

	class GitModel;
	class NetworkModel;

	// What a menu command needs of one of the window bar's buttons.
	struct NetworkCommandState
	{
		bool Enabled = false;

		// Why it is disabled, in the bar's own words; empty while enabled or while the bar says nothing yet.
		std::string Note;
	};

	// Fetch, Pull and Push are enabled exactly as the bar's buttons are, which is decided by DescribeNetworkButtons alone.
	// A button that runs is enabled in the bar to cancel its operation; the command is not, since it starts one.
	// Push with several remotes and no upstream on a remote asks which remote, which only the bar's menu can: the command
	// is disabled, saying so.
	NetworkCommandState CommandStateOfFetch(const NetworkButtons& buttons);
	NetworkCommandState CommandStateOfPull(const NetworkButtons& buttons);
	NetworkCommandState CommandStateOfPush(const NetworkButtons& buttons);

	// The history banner's Fetch full history, which is a fetch: disabled for Fetch's own reasons (git missing, no remote, a
	// folder not trusted), in Fetch's words, and while any operation runs, the full history's own included, saying which.
	NetworkCommandState CommandStateOfFullHistory(const NetworkButtons& buttons, const NetworkState& state);

	// "Choose the remote from Push in the Git window."
	std::string ChooseRemoteFromBarText();

	// The buttons as the window bar draws them now.
	NetworkButtons DescribeNetworkButtonsNow(const GitModel& git, const NetworkModel& network, bool checkingOut);

	struct NetworkCommandServices
	{
		// The buttons as the bar draws them now; read each time a command's state or action is asked for.
		std::function<NetworkButtons()> Buttons;

		std::function<void()> Fetch;
		std::function<void()> Pull;
		std::function<void()> Push;
	};

	// git.fetch, git.pull and git.push, with no default shortcuts. They are listed in the Git menu.
	void RegisterNetworkCommands(Core::CommandRegistry& commands, NetworkCommandServices services);
}
