#include "GraphiteMenu.h"

namespace graphite
{
    std::vector<core::MenuItem> BuildMenuTree()
    {
        using core::MenuItem;

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
                    // Nested under Git so it is obvious whose rules decide what is
                    // ignored — the answer comes from .gitignore, .git/info/exclude and
                    // the user's global configuration, and nothing in "Ignored Files"
                    // alone said so. The submenu is also where a branch indicator and
                    // file status belong when they arrive.
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
