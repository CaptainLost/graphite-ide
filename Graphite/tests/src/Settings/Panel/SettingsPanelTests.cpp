#include <doctest/doctest.h>

#include "FakeSettingsPage.h"
#include "ImGuiFrames.h"

#include "Settings/Panel/SettingsPanel.h"
#include "Theme/ActiveScheme.h"

#include "Core/Base/Base.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/KeyEvent.h"
#include "Core/ImGui/Animation.h"
#include "Core/ImGui/Toggle.h"

#include <imgui.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

	// What the fake pages' controls write to, so a test can see a click land.
	struct ControlState
	{
		bool WordWrap = false;
		ImVec2 WordWrapCentre;
		ImVec2 PaddingInside;
	};

	void DrawWordWrap(ControlState& state)
	{
		state.PaddingInside = ImGui::GetStyle().WindowPadding;
		Core::Toggle("##wordWrap", &state.WordWrap);
		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();
		state.WordWrapCentre = ImVec2((itemMin.x + itemMax.x) * 0.5f, (itemMin.y + itemMax.y) * 0.5f);
	}

	void DrawTable()
	{
		ImGui::TextUnformatted("table");
	}

	std::string CommandsMatching(std::string_view query)
	{
		return query == "save" ? "1 command matches" : "";
	}

	void DrawInWindow(Graphite::SettingsPanel& panel, float width)
	{
		Core::Animation::BeginFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(width, 600.0f));
		ImGui::Begin("Settings");
		panel.OnImGuiRender();
		ImGui::End();
	}

	void Press(Graphite::SettingsPanel& panel)
	{
		Core::KeyPressedEvent event{Core::KeyCode::A, Core::KeyModifier::None, false};
		panel.OnEvent(event);
	}

	// General: Key Bindings, Language Servers (C++, C#). Panels: Editor, Git.
	struct PanelFixture
	{
		ControlState Controls;
		FakeSettingsPage* KeyBindings = nullptr;
		FakeSettingsPage* Servers = nullptr;
		FakeSettingsPage* Cpp = nullptr;
		FakeSettingsPage* CSharp = nullptr;
		FakeSettingsPage* Editor = nullptr;
		FakeSettingsPage* Git = nullptr;
		Graphite::ActiveScheme Scheme;
		Graphite::SettingsPanel Panel;
		ImGuiFrames Frames;

		PanelFixture() : Panel(Build(), Scheme) {}

		Graphite::SettingsDirectory Build()
		{
			Graphite::SettingsCard text{.Heading = "Text"};
			text.Rows.push_back(Graphite::SettingsRow{.Title = "Word wrap",
													  .Description = "Long lines continue on the next line.",
													  .ControlWidth = Core::kToggleWidth,
													  .DrawControl = [this] { DrawWordWrap(Controls); }});

			Graphite::SettingsCard diffs{.Heading = "Diffs"};
			diffs.Rows.push_back(Graphite::SettingsRow{.Title = "Wrap long lines", .Description = "The diff toolbar has the same switch."});

			Graphite::SettingsCard shortcuts{.Heading = "Shortcuts"};
			shortcuts.Rows.push_back(Graphite::SettingsRow{
				.Title = "Shortcuts", .Keywords = {"shortcut"}, .DrawBlock = DrawTable, .FillsHeight = true, .DescribeMatches = CommandsMatching});

			Graphite::SettingsCard server{.Heading = "Server"};
			server.Rows.push_back(Graphite::SettingsRow{.Title = "Language server", .Description = "Roslyn is recommended."});

			Core::Scope<FakeSettingsPage> cpp = Core::CreateScope<FakeSettingsPage>("Cpp", "C++", std::vector<Graphite::SettingsCard>{server});
			Core::Scope<FakeSettingsPage> csharp = Core::CreateScope<FakeSettingsPage>("CSharp", "C#", std::vector<Graphite::SettingsCard>{server});
			Cpp = cpp.get();
			CSharp = csharp.get();

			Graphite::SettingsGroup languages = GroupOf("");
			languages.Pages.push_back(std::move(cpp));
			languages.Pages.push_back(std::move(csharp));
			std::vector<Graphite::SettingsGroup> childGroups;
			childGroups.push_back(std::move(languages));

			Core::Scope<FakeSettingsPage> servers = Core::CreateScope<FakeSettingsPage>("languageServers", "Language Servers");
			servers->SetChildren(Graphite::SettingsDirectory{std::move(childGroups)});
			Servers = servers.get();

			Core::Scope<FakeSettingsPage> keyBindings =
				Core::CreateScope<FakeSettingsPage>("keyBindings", "Key Bindings", std::vector<Graphite::SettingsCard>{shortcuts});
			Core::Scope<FakeSettingsPage> editor = Core::CreateScope<FakeSettingsPage>("editor", "Editor", std::vector<Graphite::SettingsCard>{text});
			Core::Scope<FakeSettingsPage> git = Core::CreateScope<FakeSettingsPage>("git", "Git", std::vector<Graphite::SettingsCard>{diffs});
			KeyBindings = keyBindings.get();
			Editor = editor.get();
			Git = git.get();

			Graphite::SettingsGroup general = GroupOf("General");
			general.Pages.push_back(std::move(keyBindings));
			general.Pages.push_back(std::move(servers));

			Graphite::SettingsGroup panels = GroupOf("Panels");
			panels.Pages.push_back(std::move(editor));
			panels.Pages.push_back(std::move(git));

			std::vector<Graphite::SettingsGroup> groups;
			groups.push_back(std::move(general));
			groups.push_back(std::move(panels));

			return Graphite::SettingsDirectory{std::move(groups)};
		}

		std::vector<FakeSettingsPage*> AllPages() const { return {KeyBindings, Servers, Cpp, CSharp, Editor, Git}; }

		void DrawFrames(float width = 1000.0f)
		{
			Frames.Next([this, width] { DrawInWindow(Panel, width); });
			Frames.Next([this, width] { DrawInWindow(Panel, width); });
		}
	};
}

TEST_CASE("SettingsPanel opens on the first page of its first group")
{
	PanelFixture fixture;

	CHECK(fixture.Panel.GetOpenPath() == std::vector<std::string>{"keyBindings"});
}

// The rule that keeps the key binding editor from swallowing keystrokes aimed at a page the user is looking at.
TEST_CASE("SettingsPanel offers an event to the open page only")
{
	PanelFixture fixture;

	Press(fixture.Panel);
	CHECK(fixture.KeyBindings->GetEventCount() == 1);
	CHECK(fixture.Editor->GetEventCount() == 0);

	REQUIRE(fixture.Panel.Open({"editor"}));
	Press(fixture.Panel);
	CHECK(fixture.KeyBindings->GetEventCount() == 1);
	CHECK(fixture.Editor->GetEventCount() == 1);
}

TEST_CASE("SettingsPanel ignores a path that names no page")
{
	PanelFixture fixture;

	CHECK_FALSE(fixture.Panel.Open({"missing"}));
	CHECK_FALSE(fixture.Panel.Open({"languageServers", "Rust"}));
	CHECK_FALSE(fixture.Panel.Open({"editor", "deeper"}));
	CHECK_FALSE(fixture.Panel.Open({}));
	CHECK(fixture.Panel.GetOpenPath() == std::vector<std::string>{"keyBindings"});
}

TEST_CASE("SettingsPanel with no pages answers without indexing anything")
{
	Graphite::ActiveScheme scheme;
	Graphite::SettingsPanel panel{Graphite::SettingsDirectory{}, scheme};
	ImGuiFrames frames;

	Press(panel);
	frames.Next([&panel] { DrawInWindow(panel, 800.0f); });

	CHECK(panel.GetOpenPath().empty());
	CHECK_FALSE(panel.Open({"editor"}));
}

TEST_CASE("SettingsPanel tells a page it is being left")
{
	PanelFixture fixture;

	REQUIRE(fixture.Panel.Open({"editor"}));

	CHECK(fixture.KeyBindings->GetDeactivationCount() == 1);
	CHECK(fixture.Editor->GetDeactivationCount() == 0);
}

// Clicking the highlighted entry is not a change, and would otherwise drop a chord capture in progress.
TEST_CASE("SettingsPanel does not deactivate a page opened again")
{
	PanelFixture fixture;

	REQUIRE(fixture.Panel.Open({"keyBindings"}));

	CHECK(fixture.KeyBindings->GetDeactivationCount() == 0);
}

TEST_CASE("SettingsPanel opens a page inside a page by its path")
{
	PanelFixture fixture;

	REQUIRE(fixture.Panel.Open({"languageServers", "CSharp"}));

	CHECK(fixture.Panel.GetOpenPath() == std::vector<std::string>{"languageServers", "CSharp"});
	Press(fixture.Panel);
	CHECK(fixture.CSharp->GetEventCount() == 1);
	CHECK(fixture.Servers->GetEventCount() == 0);
}

TEST_CASE("Opening a page that holds pages shows the one shown there last")
{
	PanelFixture fixture;

	REQUIRE(fixture.Panel.Open({"languageServers"}));
	CHECK(fixture.Panel.GetOpenPath() == std::vector<std::string>{"languageServers", "Cpp"});

	REQUIRE(fixture.Panel.Open({"languageServers", "CSharp"}));
	REQUIRE(fixture.Panel.Open({"editor"}));
	REQUIRE(fixture.Panel.Open({"languageServers"}));

	CHECK(fixture.Panel.GetOpenPath() == std::vector<std::string>{"languageServers", "CSharp"});
}

TEST_CASE("Moving between the pages inside a page tells the one left")
{
	PanelFixture fixture;

	REQUIRE(fixture.Panel.Open({"languageServers", "Cpp"}));
	REQUIRE(fixture.Panel.Open({"languageServers", "CSharp"}));

	CHECK(fixture.Cpp->GetDeactivationCount() == 1);
	CHECK(fixture.CSharp->GetDeactivationCount() == 0);
}

TEST_CASE("Starting a search deactivates the open page and stops its events")
{
	PanelFixture fixture;

	fixture.Panel.SetQuery("wrap");

	CHECK(fixture.Panel.IsSearching());
	CHECK(fixture.KeyBindings->GetDeactivationCount() == 1);
	Press(fixture.Panel);
	CHECK(fixture.KeyBindings->GetEventCount() == 0);

	fixture.Panel.SetQuery("wrap long");
	CHECK(fixture.KeyBindings->GetDeactivationCount() == 1);
}

TEST_CASE("Clearing a search returns to the page it began on")
{
	PanelFixture fixture;
	REQUIRE(fixture.Panel.Open({"git"}));

	fixture.Panel.SetQuery("wrap");
	fixture.Panel.SetQuery("");

	CHECK_FALSE(fixture.Panel.IsSearching());
	CHECK(fixture.Panel.GetOpenPath() == std::vector<std::string>{"git"});
	Press(fixture.Panel);
	CHECK(fixture.Git->GetEventCount() == 1);
}

TEST_CASE("Opening a page ends a search")
{
	PanelFixture fixture;
	fixture.Panel.SetQuery("wrap");

	REQUIRE(fixture.Panel.Open({"editor"}));

	CHECK(fixture.Panel.GetQuery().empty());
	CHECK_FALSE(fixture.Panel.IsSearching());
}

TEST_CASE("A query of spaces is not a search")
{
	PanelFixture fixture;

	fixture.Panel.SetQuery("   ");

	CHECK_FALSE(fixture.Panel.IsSearching());
	CHECK(fixture.KeyBindings->GetDeactivationCount() == 0);
	Press(fixture.Panel);
	CHECK(fixture.KeyBindings->GetEventCount() == 1);
}

TEST_CASE("Hiding the panel deactivates every page, open or not")
{
	PanelFixture fixture;

	fixture.Panel.SetDrawnOnLatestFrame(true);
	fixture.Panel.OnUpdate(Core::Timestep{});
	fixture.Panel.SetDrawnOnLatestFrame(false);
	fixture.Panel.OnUpdate(Core::Timestep{});
	fixture.Panel.OnUpdate(Core::Timestep{});

	for (const FakeSettingsPage* page : fixture.AllPages())
	{
		CHECK(page->GetDeactivationCount() == 1);
	}
}

TEST_CASE("Opening a block found by what it holds tells its page the query")
{
	PanelFixture fixture;
	REQUIRE(fixture.Panel.Open({"editor"}));
	fixture.Panel.SetQuery(" save ");

	fixture.Panel.OpenSearchHit({"keyBindings"}, "1 command matches");

	CHECK(fixture.Panel.GetOpenPath() == std::vector<std::string>{"keyBindings"});
	CHECK(fixture.KeyBindings->GetOpenedFromSearch() == std::vector<std::string>{"save"});
	CHECK_FALSE(fixture.Panel.IsSearching());
}

TEST_CASE("Opening a block found by its title opens it unfiltered")
{
	PanelFixture fixture;
	fixture.Panel.SetQuery("shortcut");

	fixture.Panel.OpenSearchHit({"keyBindings"}, "");

	CHECK(fixture.Panel.GetOpenPath() == std::vector<std::string>{"keyBindings"});
	CHECK(fixture.KeyBindings->GetOpenedFromSearch().empty());
}

TEST_CASE("Every page is updated and asked for its dialogs each frame, open or not")
{
	PanelFixture fixture;

	fixture.Frames.Next([&fixture] { DrawInWindow(fixture.Panel, 1000.0f); });

	for (const FakeSettingsPage* page : fixture.AllPages())
	{
		CHECK(page->GetUpdateCount() == 1);
		CHECK(page->GetDialogRenderCount() == 1);
	}
}

TEST_CASE("The panel draws a page, a page holding pages and search results, wide and narrow, without an ImGui assertion")
{
	PanelFixture fixture;

	fixture.DrawFrames();
	REQUIRE(fixture.Panel.Open({"editor"}));
	fixture.DrawFrames();
	fixture.DrawFrames(500.0f);
	REQUIRE(fixture.Panel.Open({"languageServers", "CSharp"}));
	fixture.DrawFrames();
	fixture.Panel.SetQuery("wrap");
	fixture.DrawFrames();
	fixture.DrawFrames(500.0f);
	fixture.Panel.SetQuery("nothing matches this");
	fixture.DrawFrames();

	CHECK(fixture.Panel.IsSearching());
}

TEST_CASE("A toggle in the search results changes its setting")
{
	PanelFixture fixture;
	fixture.Panel.SetQuery("wrap");
	fixture.DrawFrames();
	REQUIRE(fixture.Controls.WordWrapCentre.x > 0.0f);

	fixture.Frames.Click(fixture.Controls.WordWrapCentre, [&fixture] { DrawInWindow(fixture.Panel, 1000.0f); });

	CHECK(fixture.Controls.WordWrap);
}

// ImGui reads the padding at Begin; what is drawn inside the page copies it for its combos, tooltips and dialogs.
TEST_CASE("The padding of the panel's child windows does not reach what is drawn inside them")
{
	PanelFixture fixture;
	REQUIRE(fixture.Panel.Open({"editor"}));
	fixture.DrawFrames();
	const ImVec2 outside = ImGui::GetStyle().WindowPadding;

	CHECK(fixture.Controls.PaddingInside.x == outside.x);
	CHECK(fixture.Controls.PaddingInside.y == outside.y);

	fixture.Controls.PaddingInside = ImVec2(-1.0f, -1.0f);
	fixture.Panel.SetQuery("wrap");
	fixture.DrawFrames();

	CHECK(fixture.Controls.PaddingInside.x == outside.x);
	CHECK(fixture.Controls.PaddingInside.y == outside.y);
}