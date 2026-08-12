#include <doctest/doctest.h>

#include "Core/Command/CommandRegistry.h"
#include "Core/Ui/PanelManager.h"

#include <memory>
#include <string>
#include <vector>

namespace {
	class CountingPanel final : public Core::Panel
	{
	public:
		CountingPanel(std::string id, std::string title, bool visible = true)
			: Core::Panel(std::move(id), std::move(title), visible)
		{
		}

		void OnUpdate(Core::Timestep) override { ++updates; }
		void OnNotDrawn() override { ++notDrawn; }

		int updates = 0;
		int notDrawn = 0;
	};

	class CapturingPanel final : public Core::Panel
	{
	public:
		CapturingPanel(std::string id, bool capturing)
			: Core::Panel(std::move(id), "Panel"), m_Capturing(capturing)
		{
		}

		bool IsCapturingInput() const override { return m_Capturing; }

	private:
		bool m_Capturing = false;
	};
}

TEST_CASE("Add registers a panel and allows finding it by identifier")
{
	Core::PanelManager panels;

	Core::Panel* added = panels.Add(std::make_unique<CountingPanel>("console", "Console"));

	REQUIRE(added != nullptr);
	CHECK(panels.GetCount() == 1);
	CHECK(panels.Find("console") == added);
	CHECK(added->GetTitle() == "Console");
	CHECK(added->IsVisible());
}

TEST_CASE("Add rejects a duplicate identifier")
{
	Core::PanelManager panels;
	panels.Add(std::make_unique<CountingPanel>("console", "Console"));

	Core::Panel* duplicate = panels.Add(std::make_unique<CountingPanel>("console", "Other"));

	CHECK(duplicate == nullptr);
	CHECK(panels.GetCount() == 1);
}

TEST_CASE("Find returns nullptr for an unknown identifier")
{
	Core::PanelManager panels;

	CHECK(panels.Find("missing") == nullptr);
}

TEST_CASE("SetVisible changes visibility and signals an unknown identifier")
{
	Core::PanelManager panels;
	Core::Panel* panel = panels.Add(std::make_unique<CountingPanel>("files", "Files"));

	CHECK(panels.SetVisible("files", false));
	CHECK_FALSE(panel->IsVisible());
	CHECK_FALSE(panels.SetVisible("missing", true));
}

TEST_CASE("OnUpdate reaches all panels, including hidden ones")
{
	Core::PanelManager panels;
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
	Core::PanelManager panels;
	Core::CommandRegistry commands;
	panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
	panels.Add(std::make_unique<CountingPanel>("console", "Console"));

	panels.RegisterCommands(commands);

	const std::vector<const Core::Command*> members = commands.InGroup("panels");

	REQUIRE(members.size() == 2);
	CHECK(members[0]->id == "panel.files");
	CHECK(members[0]->label == "Explorer");
	CHECK(members[1]->id == "panel.console");
}

TEST_CASE("A panel's command toggles it and reports its state")
{
	Core::PanelManager panels;
	Core::CommandRegistry commands;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
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
	Core::PanelManager panels;
	Core::CommandRegistry commands;
	panels.RegisterCommands(commands);

	panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));

	REQUIRE(commands.Find("panel.files") != nullptr);
}

TEST_CASE("IsCapturingInput answers for any panel reading raw keys")
{
	// What the application asks before routing shortcuts: a chord being recorded for a
	// rebinding must not also run the command it is being taken from. A panel that
	// captures nothing is the default, so the ordinary case has to stay false.
	Core::PanelManager panels;
	panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));

	CHECK_FALSE(panels.IsCapturingInput());

	panels.Add(std::make_unique<CapturingPanel>("keybindings", true));

	CHECK(panels.IsCapturingInput());
}

TEST_CASE("IsCapturingInput ignores a hidden panel")
{
	// A hidden panel draws nothing and so can be reading nothing, whatever it left behind
	// in its own state. Were it believed, closing the screen mid-capture would leave every
	// shortcut in the application dead with nothing on screen to explain it.
	Core::PanelManager panels;
	Core::Panel* keys = panels.Add(std::make_unique<CapturingPanel>("keybindings", true));
	keys->SetVisible(false);

	CHECK_FALSE(panels.IsCapturingInput());
}

TEST_CASE("A hidden panel is told it was not drawn")
{
	// The whole point of OnNotDrawn: a screen in the middle of something — a shortcut
	// capture waiting for a key — must be told the frame it never got, or it waits
	// invisibly with nothing on screen able to end it. The hidden branch reaches this
	// before any ImGui call, so it needs no context or frame to be checked here.
	Core::PanelManager panels;
	auto* hidden =
		static_cast<CountingPanel*>(panels.Add(std::make_unique<CountingPanel>("keybindings",
																			   "Keyboard Shortcuts",
																			   false)));

	REQUIRE_FALSE(hidden->IsVisible());

	panels.OnUIRender();

	CHECK(hidden->notDrawn == 1);
}

TEST_CASE("ResetVisibility restores each panel's own default, not everything visible")
{
	// A panel that starts hidden — a settings screen — must not be revealed by Reset
	// Layout, which nobody asked to open it.
	Core::PanelManager panels;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
	Core::Panel* keys = panels.Add(std::make_unique<CountingPanel>("keybindings", "Keyboard Shortcuts", false));

	REQUIRE(files->IsVisible());
	REQUIRE_FALSE(keys->IsVisible());

	files->SetVisible(false);
	keys->SetVisible(true);
	panels.ResetVisibility();

	CHECK(files->IsVisible());
	CHECK_FALSE(keys->IsVisible());
}
