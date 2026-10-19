#include "App/ToolStripContent.h"

#include <IconsLucide.h>

#include <algorithm>
#include <array>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::array<std::string_view, 3> kTopGroups = {"navigate", "git", "ai"};
		constexpr std::string_view kToolsGroup = "tools";
		constexpr std::string_view kWindowGroup = "window";

		bool IsKnownGroup(std::string_view group)
		{
			return group == kToolsGroup || group == kWindowGroup || std::find(kTopGroups.begin(), kTopGroups.end(), group) != kTopGroups.end();
		}

		Core::ToolStripItem ItemFor(const ToolStripPanel& panel, const ToolStripInputs& inputs)
		{
			Core::ToolStripItem item;
			item.Id = panel.Id;
			item.Icon = panel.Icon;
			item.Tooltip = panel.Shortcut.empty() ? panel.Title : panel.Title + " (" + panel.Shortcut + ")";
			item.Open = panel.Visible;
			if (panel.Id == "changes" && inputs.HasChanges)
			{
				item.Badge = inputs.Colours.Changes;
			}
			else if (panel.Id == "problems")
			{
				if (inputs.Errors > 0)
				{
					item.Badge = inputs.Colours.Error;
				}
				else if (inputs.Warnings > 0)
				{
					item.Badge = inputs.Colours.Warning;
				}
			}
			return item;
		}

		Core::ToolStripItem TerminalsItem(const ToolStripInputs& inputs)
		{
			Core::ToolStripItem item;
			item.Id = std::string(kTerminalsStripId);
			item.Icon = ICON_LC_SQUARE_TERMINAL;
			item.Tooltip = "Terminal";
			item.Open = inputs.Terminals.Open;
			if (inputs.Terminals.RunningHidden)
			{
				item.Badge = inputs.Colours.Running;
			}
			return item;
		}

		std::vector<Core::ToolStripItem> GroupOf(const ToolStripInputs& inputs, std::string_view group)
		{
			std::vector<Core::ToolStripItem> items;
			for (const ToolStripPanel& panel : inputs.Panels)
			{
				if (panel.MenuGroup == group)
				{
					items.push_back(ItemFor(panel, inputs));
				}
			}
			return items;
		}

		void AddIfAny(std::vector<std::vector<Core::ToolStripItem>>& groups, std::vector<Core::ToolStripItem> items)
		{
			if (!items.empty())
			{
				groups.push_back(std::move(items));
			}
		}
	}

	Core::ToolStripContent BuildToolStripContent(const ToolStripInputs& inputs)
	{
		Core::ToolStripContent content;
		for (std::string_view group : kTopGroups)
		{
			AddIfAny(content.Top, GroupOf(inputs, group));
		}

		std::vector<std::string_view> unknownGroups;
		for (const ToolStripPanel& panel : inputs.Panels)
		{
			if (!IsKnownGroup(panel.MenuGroup) && std::find(unknownGroups.begin(), unknownGroups.end(), panel.MenuGroup) == unknownGroups.end())
			{
				unknownGroups.push_back(panel.MenuGroup);
			}
		}
		for (std::string_view group : unknownGroups)
		{
			AddIfAny(content.Top, GroupOf(inputs, group));
		}

		std::vector<Core::ToolStripItem> tools = GroupOf(inputs, kToolsGroup);
		tools.insert(tools.begin(), TerminalsItem(inputs));
		content.Bottom.push_back(std::move(tools));
		AddIfAny(content.Bottom, GroupOf(inputs, kWindowGroup));
		return content;
	}
}
