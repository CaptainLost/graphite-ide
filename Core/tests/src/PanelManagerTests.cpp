#include <doctest/doctest.h>

#include "Core/Command/CommandRegistry.h"
#include "Core/Ui/PanelManager.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
    class CountingPanel final : public core::Panel
    {
    public:
        CountingPanel(std::string id, std::string title, bool visible = true)
            : core::Panel(std::move(id), std::move(title), visible)
        {
        }

        void OnUpdate(float) override { ++updates; }

        int updates = 0;
    };
}

TEST_CASE("Add registers a panel and allows finding it by identifier")
{
    core::PanelManager panels;

    core::Panel* added = panels.Add(std::make_unique<CountingPanel>("console", "Console"));

    REQUIRE(added != nullptr);
    CHECK(panels.Count() == 1);
    CHECK(panels.Find("console") == added);
    CHECK(added->Title() == "Console");
    CHECK(added->IsVisible());
}

TEST_CASE("Add rejects a duplicate identifier")
{
    core::PanelManager panels;
    panels.Add(std::make_unique<CountingPanel>("console", "Console"));

    core::Panel* duplicate = panels.Add(std::make_unique<CountingPanel>("console", "Other"));

    CHECK(duplicate == nullptr);
    CHECK(panels.Count() == 1);
}

TEST_CASE("Find returns nullptr for an unknown identifier")
{
    core::PanelManager panels;

    CHECK(panels.Find("missing") == nullptr);
}

TEST_CASE("SetVisible changes visibility and signals an unknown identifier")
{
    core::PanelManager panels;
    core::Panel* panel = panels.Add(std::make_unique<CountingPanel>("files", "Files"));

    CHECK(panels.SetVisible("files", false));
    CHECK_FALSE(panel->IsVisible());
    CHECK_FALSE(panels.SetVisible("missing", true));
}

TEST_CASE("OnUpdate reaches all panels, including hidden ones")
{
    core::PanelManager panels;
    auto* visible = static_cast<CountingPanel*>(
        panels.Add(std::make_unique<CountingPanel>("a", "A")));
    auto* hidden = static_cast<CountingPanel*>(
        panels.Add(std::make_unique<CountingPanel>("b", "B")));
    hidden->SetVisible(false);

    panels.OnUpdate(0.016f);

    CHECK(visible->updates == 1);
    CHECK(hidden->updates == 1);
}

TEST_CASE("RegisterCommands gives every panel a visibility toggle in the panels group")
{
    core::PanelManager panels;
    core::CommandRegistry commands;
    panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
    panels.Add(std::make_unique<CountingPanel>("console", "Console"));

    panels.RegisterCommands(commands);

    const std::vector<const core::Command*> members = commands.InGroup("panels");

    REQUIRE(members.size() == 2);
    CHECK(members[0]->id == "panel.files");
    CHECK(members[0]->label == "Explorer");
    CHECK(members[1]->id == "panel.console");
}

TEST_CASE("A panel's command toggles it and reports its state")
{
    core::PanelManager panels;
    core::CommandRegistry commands;
    core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
    panels.RegisterCommands(commands);

    REQUIRE(files->IsVisible());
    REQUIRE(commands.Find("panel.files")->checked());

    commands.Run("panel.files");

    CHECK_FALSE(files->IsVisible());
    CHECK_FALSE(commands.Find("panel.files")->checked());
}

TEST_CASE("A panel added after RegisterCommands still gets its command")
{
    // Panels are added in OnAttach and commands are registered there too; the order of
    // those two calls should not be a trap for whoever edits OnAttach next.
    core::PanelManager panels;
    core::CommandRegistry commands;
    panels.RegisterCommands(commands);

    panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));

    REQUIRE(commands.Find("panel.files") != nullptr);
}

TEST_CASE("ResetVisibility restores each panel's own default, not everything visible")
{
    // A panel that starts hidden — a settings screen — must not be revealed by Reset
    // Layout, which nobody asked to open it.
    core::PanelManager panels;
    core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
    core::Panel* keys = panels.Add(std::make_unique<CountingPanel>("keybindings", "Keyboard Shortcuts", false));

    REQUIRE(files->IsVisible());
    REQUIRE_FALSE(keys->IsVisible());

    files->SetVisible(false);
    keys->SetVisible(true);
    panels.ResetVisibility();

    CHECK(files->IsVisible());
    CHECK_FALSE(keys->IsVisible());
}
