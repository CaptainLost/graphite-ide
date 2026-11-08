#include "App/GraphiteMenu.h"

#include "App/SampleToasts.h"

#include <string>

namespace Graphite {

#ifdef CORE_DEBUG
	namespace {
		std::vector<Core::MenuItem> SampleToastMenuItems()
		{
			std::vector<Core::MenuItem> items;
			for (const SampleToastCommand& sample : kSampleToastCommands)
			{
				items.push_back(Core::MenuItem::Command(std::string(sample.CommandId)));
			}
			return items;
		}
	}
#endif

	std::vector<Core::MenuItem> BuildMenuTree()
	{
		using Core::MenuItem;

		return {
			MenuItem::Submenu("File",
							  {
								  MenuItem::Command("explorer.newFile"),
								  MenuItem::Command("explorer.newFolder"),
								  MenuItem::Separator(),
								  MenuItem::Command("workspace.openFolder"),
								  MenuItem::Command("workspace.openSolution"),
								  MenuItem::Submenu("Open Recent", {MenuItem::Group("workspace.recent")}),
								  MenuItem::Separator(),
								  MenuItem::Command("workspace.close"),
								  MenuItem::Command("workspace.toggleTrust"),
								  MenuItem::Separator(),
								  MenuItem::Command("document.save"),
								  MenuItem::Command("document.saveAs"),
								  MenuItem::Command("document.saveAll"),
								  MenuItem::Command("document.close"),
								  MenuItem::Separator(),
								  MenuItem::Command("app.exit"),
							  }),
			MenuItem::Submenu(
				"View",
				{
					MenuItem::Submenu("Panels",
									  {
										  MenuItem::Section("Navigate"),
										  MenuItem::Group("panels.navigate"),
										  MenuItem::Section("Tools"),
										  MenuItem::Group("panels.tools"),
										  MenuItem::Section("Git"),
										  MenuItem::Group("panels.git"),
										  MenuItem::Section("AI"),
										  MenuItem::Group("panels.ai"),
									  }),
					MenuItem::Command("terminal.new"),
					MenuItem::Submenu("New Terminal With", {MenuItem::Group("terminal.shells")}),
					MenuItem::Separator(),
					MenuItem::Command("editor.toggleWordWrap"),
					MenuItem::Command("editor.zoomIn"),
					MenuItem::Command("editor.zoomOut"),
					MenuItem::Command("editor.resetZoom"),
					MenuItem::Submenu(
						"Explorer Options",
						{
							MenuItem::Submenu("Git-Ignored Files",
											  {
												  MenuItem::Command("git.ignored.show"),
												  MenuItem::Command("git.ignored.dim"),
												  MenuItem::Command("git.ignored.hide"),
											  }),
							MenuItem::Command("explorer.followActiveFile"),
						}),
					MenuItem::Separator(),
					MenuItem::Group("panels.window"),
					MenuItem::Command("view.resetLayout"),
				}),
			MenuItem::Submenu("Go", {MenuItem::Command("file.quickOpen"), MenuItem::Command("editor.goToLine")}),
			MenuItem::Submenu("Git", {MenuItem::Command("git.fetch"), MenuItem::Command("git.pull"), MenuItem::Command("git.push"),
											 MenuItem::Command("git.forcePush")}),
			MenuItem::Submenu("Help", {MenuItem::Command("help.about")}),
#ifdef CORE_DEBUG
			MenuItem::Submenu("Debug",
							  {
								  MenuItem::Command("debug.logTestEntries"),
								  MenuItem::Separator(),
								  MenuItem::Submenu("Show Toast", SampleToastMenuItems()),
							  }),
#endif
		};
	}
}