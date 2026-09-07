#include <doctest/doctest.h>

#include "Core/Command/CommandRegistry.h"
#include "Core/UI/PanelManager.h"

#include <memory>
#include <string>
#include <vector>

namespace {
	class CountingPanel final : public Core::Panel
	{
	public:
		CountingPanel(std::string id, std::string title, std::string menuGroup = "tools",
					  bool visible = true)
			: Core::Panel(std::move(id), std::move(title), std::move(menuGroup), visible)
		{
		}

		void OnUpdate(Core::Timestep) override { ++updates; }

		int updates = 0;
	};

	// Counts what reaches it and can be told to consume, which is the whole contract a panel
	// has with the event walk.
	class ListeningPanel final : public Core::Panel
	{
	public:
		ListeningPanel(std::string id, bool consume)
			: Core::Panel(std::move(id), "Panel", "tools"), m_Consume(consume)
		{
		}

		void OnEvent(Core::Event& event) override
		{
			++seen;
			event.Handled = m_Consume;
		}

		int seen = 0;

	private:
		bool m_Consume = false;
	};

	// Written out rather than declared with the EVENT_CLASS_* macros: those name EventType
	// unqualified, which only resolves inside namespace Core.
	class TestEvent final : public Core::Event
	{
	public:
		static Core::EventType GetStaticType() { return Core::EventType::None; }
		Core::EventType GetEventType() const override { return GetStaticType(); }
		const char* GetName() const override { return "TestEvent"; }
		int GetCategoryFlags() const override { return Core::EventCategoryApplication; }
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

	const std::vector<const Core::Command*> members = commands.InGroup("panels.tools");

	REQUIRE(members.size() == 2);
	CHECK(members[0]->Id == "panel.files");
	CHECK(members[0]->Label == "Explorer");
	CHECK(members[1]->Id == "panel.console");
}

TEST_CASE("A panel's toggle lands in the menu group the panel names")
{
	// The menu draws one section per group, so this is what decides where a panel appears
	// in it - not the order the panels were added in.
	Core::PanelManager panels;
	Core::CommandRegistry commands;
	panels.Add(std::make_unique<CountingPanel>("files", "Explorer", "navigate"));
	panels.Add(std::make_unique<CountingPanel>("console", "Console", "tools"));

	panels.RegisterCommands(commands);

	const std::vector<const Core::Command*> navigate = commands.InGroup("panels.navigate");
	const std::vector<const Core::Command*> tools = commands.InGroup("panels.tools");

	REQUIRE(navigate.size() == 1);
	CHECK(navigate[0]->Id == "panel.files");
	REQUIRE(tools.size() == 1);
	CHECK(tools[0]->Id == "panel.console");
}

TEST_CASE("A panel's command toggles it and reports its state")
{
	Core::PanelManager panels;
	Core::CommandRegistry commands;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
	panels.RegisterCommands(commands);

	REQUIRE(files->IsVisible());
	REQUIRE(commands.Find("panel.files")->Checked());

	commands.Run("panel.files");

	CHECK_FALSE(files->IsVisible());
	CHECK_FALSE(commands.Find("panel.files")->Checked());
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

TEST_CASE("An event reaches only the panels that drew")
{
	// A panel behind another tab of the same dock node is not on screen for the user to have
	// aimed at. Were it given the event anyway, a shortcut capture left waiting on a hidden
	// screen would swallow every keystroke in the application with nothing on screen to
	// explain it.
	Core::PanelManager panels;
	auto* drawn = static_cast<ListeningPanel*>(panels.Add(std::make_unique<ListeningPanel>("files", false)));
	auto* hidden =
		static_cast<ListeningPanel*>(panels.Add(std::make_unique<ListeningPanel>("keybindings", false)));

	drawn->SetWasDrawn(true);
	hidden->SetWasDrawn(false);

	TestEvent event;
	panels.OnEvent(event);

	CHECK(drawn->seen == 1);
	CHECK(hidden->seen == 0);
}

TEST_CASE("A panel that consumes an event stops it reaching the rest")
{
	// What replaces asking every panel whether it is capturing: the panel recording a chord
	// simply takes the keystroke, and nothing below — the shortcut router included — sees it.
	Core::PanelManager panels;
	auto* first = static_cast<ListeningPanel*>(panels.Add(std::make_unique<ListeningPanel>("first", true)));
	auto* second =
		static_cast<ListeningPanel*>(panels.Add(std::make_unique<ListeningPanel>("second", false)));

	first->SetWasDrawn(true);
	second->SetWasDrawn(true);

	TestEvent event;
	panels.OnEvent(event);

	CHECK(first->seen == 1);
	CHECK(second->seen == 0);
	CHECK(event.Handled);
}

TEST_CASE("A panel starts out not drawn")
{
	// Events arrive before the first frame is drawn. A panel that claimed to have been on
	// screen before it ever was would answer a keystroke aimed at nothing.
	Core::PanelManager panels;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));

	CHECK_FALSE(files->WasDrawn());
}

TEST_CASE("ResetVisibility restores each panel's own default, not everything visible")
{
	// A panel that starts hidden — a settings screen — must not be revealed by Reset
	// Layout, which nobody asked to open it.
	Core::PanelManager panels;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
	Core::Panel* keys = panels.Add(std::make_unique<CountingPanel>("keybindings", "Keyboard Shortcuts", "tools", false));

	REQUIRE(files->IsVisible());
	REQUIRE_FALSE(keys->IsVisible());

	files->SetVisible(false);
	keys->SetVisible(true);
	panels.ResetVisibility();

	CHECK(files->IsVisible());
	CHECK_FALSE(keys->IsVisible());
}
