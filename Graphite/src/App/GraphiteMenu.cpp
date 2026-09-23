#include "App/GraphiteMenu.h"

namespace Graphite {

	std::vector<Core::MenuItem> BuildMenuTree()
	{
		using Core::MenuItem;

		return {
			MenuItem::Submenu("File",
							  {
								  MenuItem::Command("workspace.openFolder"),
								  MenuItem::Command("workspace.openSolution"),
								  MenuItem::Separator(),
								  MenuItem::Command("workspace.close"),
								  MenuItem::Separator(),
								  MenuItem::Command("document.saveAll"),
								  MenuItem::Separator(),
								  MenuItem::Command("app.exit"),
							  }),
			MenuItem::Submenu(
				"View",
				{
					// The panels are the bulk of this menu and the reason it is opened, but
					// they are a wall of nine rows; behind one door they stay one hover away
					// while the top level keeps what is read rather than hunted for.
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
					MenuItem::Separator(),
					MenuItem::Command("editor.toggleWordWrap"),
					MenuItem::Command("editor.zoomIn"),
					MenuItem::Command("editor.zoomOut"),
					MenuItem::Command("editor.resetZoom"),
					// The explorer is the only place these decide anything, so the way down is
					// named after it rather than after git, whose name in this menu is a
					// panel. The inner heading is what says which files are meant.
					MenuItem::Submenu(
						"Explorer Options",
						{
							MenuItem::Submenu("Git-Ignored Files",
											  {
												  MenuItem::Command("git.ignored.show"),
												  MenuItem::Command("git.ignored.dim"),
												  MenuItem::Command("git.ignored.hide"),
											  }),
						}),
					MenuItem::Separator(),
					// Settings is a panel like the rest, but it is how the application is
					// configured rather than a view of the work, so it is reached from here
					// instead of from behind Panels.
					MenuItem::Group("panels.window"),
					MenuItem::Command("view.resetLayout"),
				}),
			MenuItem::Submenu("Help",
							  {
								  MenuItem::Command("help.logTestEntries"),
							  }),
		};
	}
}