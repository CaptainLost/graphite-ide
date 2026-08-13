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
					MenuItem::Group("panels"),
					MenuItem::Separator(),
					MenuItem::Command("editor.toggleWordWrap"),
					MenuItem::Submenu("Git",
									  {
										  MenuItem::Submenu("Ignored Files",
															{
																MenuItem::Command("git.ignored.show"),
																MenuItem::Command("git.ignored.dim"),
																MenuItem::Command("git.ignored.hide"),
															}),
									  }),
					MenuItem::Separator(),
					MenuItem::Command("editor.zoomIn"),
					MenuItem::Command("editor.zoomOut"),
					MenuItem::Command("editor.resetZoom"),
					MenuItem::Separator(),
					MenuItem::Command("view.resetLayout"),
				}),
			MenuItem::Submenu("Help",
							  {
								  MenuItem::Command("help.logTestEntries"),
							  }),
		};
	}
}