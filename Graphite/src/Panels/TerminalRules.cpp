#include "Panels/TerminalRules.h"

#include "Core/Base/Ascii.h"

#include <algorithm>
#include <cctype>

namespace Graphite {

	std::string TerminalTitleFor(const std::string& title, const std::string& profileName)
	{
		if (title.empty())
		{
			return profileName;
		}

		// Only the last component can carry meaning; the directories never do.
		const std::size_t separator = title.find_last_of("\\/");
		const std::string leaf = separator == std::string::npos ? title : title.substr(separator + 1);

		if (leaf.empty())
		{
			return profileName;
		}

		return Core::Lowered(leaf).ends_with(".exe") ? profileName : leaf;
	}

	std::string NewTerminalCommandId(std::string_view profileName)
	{
		std::string slug;
		bool pendingDash = false;

		for (const char c : profileName)
		{
			const auto byte = static_cast<unsigned char>(c);

			if (std::isalnum(byte) != 0)
			{
				if (pendingDash && !slug.empty())
				{
					slug.push_back('-');
				}

				pendingDash = false;
				slug.push_back(static_cast<char>(std::tolower(byte)));
			}
			else
			{
				pendingDash = true;
			}
		}

		return "terminal.new." + (slug.empty() ? std::string{"shell"} : slug);
	}

	std::vector<TerminalTabState> CaptureTerminals(const std::vector<OpenTerminal>& open)
	{
		std::vector<TerminalTabState> kept;

		for (const OpenTerminal& terminal : open)
		{
			if (terminal.FromCommand || terminal.CarriedOver)
			{
				continue;
			}

			kept.push_back(TerminalTabState{terminal.Id, terminal.ProfileName, terminal.WorkingDirectory});
		}

		return kept;
	}

	TerminalRestorePlan PlanTerminalRestore(
		const std::vector<TerminalTabState>& saved, const std::vector<Core::ShellProfile>& shells,
		const std::vector<OpenTerminal>& open,
		const std::function<std::filesystem::path(const std::filesystem::path&)>& resolveDirectory)
	{
		TerminalRestorePlan plan;

		const auto isOpen = [&open](const std::string& id) {
			return std::any_of(open.begin(), open.end(), [&id](const OpenTerminal& terminal) { return terminal.Id == id; });
		};

		const auto isPlanned = [&plan](const std::string& id) {
			return std::any_of(plan.Start.begin(), plan.Start.end(),
							   [&id](const TerminalToStart& start) { return start.Id == id; });
		};

		for (const TerminalTabState& terminal : saved)
		{
			if (isOpen(terminal.Id))
			{
				plan.AlreadyOpen.push_back(terminal.Id);
				continue;
			}

			// Two windows under one identity would be one window to ImGui.
			if (isPlanned(terminal.Id))
			{
				continue;
			}

			const auto shell = std::find_if(shells.begin(), shells.end(), [&terminal](const Core::ShellProfile& profile) {
				return profile.Name == terminal.ProfileName;
			});

			if (shell == shells.end())
			{
				plan.UnknownProfiles.push_back(terminal.ProfileName);
				continue;
			}

			plan.Start.push_back(TerminalToStart{terminal.Id,
												 static_cast<std::size_t>(shell - shells.begin()),
												 resolveDirectory(terminal.WorkingDirectory)});
		}

		return plan;
	}

	bool OpensDefaultTerminal(const std::optional<std::vector<TerminalTabState>>& saved,
							  std::size_t openCount)
	{
		return !saved.has_value() && openCount == 0;
	}
}
