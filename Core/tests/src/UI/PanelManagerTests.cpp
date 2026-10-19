#include <doctest/doctest.h>

#include "Core/Command/CommandRegistry.h"
#include "Core/ImGui/DockSpaceHost.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/UI/PanelManager.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {
	constexpr const char* kNoIcon = "";
	constexpr Core::Timestep kOneFrame{0.016f};

	using StartsAs = Core::Panel::StartsAs;
	using Sides = Core::Panel::Sides;

	enum class EventResponse
	{
		PassOn,
		Consume
	};

	class CountingPanel final : public Core::Panel
	{
	public:
		CountingPanel(std::string id, std::string title, std::string menuGroup = "tools",
					  StartsAs initialVisibility = StartsAs::Visible, Sides sides = Sides::Padded)
			: Core::Panel(std::move(id), std::move(title), std::move(menuGroup), kNoIcon, initialVisibility, sides)
		{
		}

		void OnUpdate(Core::Timestep) override { ++updatesReceived; }

		int updatesReceived = 0;
	};

	class ListeningPanel final : public Core::Panel
	{
	public:
		ListeningPanel(std::string id, EventResponse response)
			: Core::Panel(std::move(id), "Panel", "tools", kNoIcon), m_Response(response)
		{
		}

		void OnEvent(Core::Event& event) override
		{
			++eventsReceived;
			event.Handled = m_Response == EventResponse::Consume;
		}

		int eventsReceived = 0;

	private:
		EventResponse m_Response = EventResponse::PassOn;
	};

	class IconPanel final : public Core::Panel
	{
	public:
		IconPanel() : Core::Panel("terminal", "Terminal", "tools", "*") {}
	};

	// Written out because CORE_EVENT_CLASS_TYPE names EventType unqualified, which resolves
	// only inside namespace Core.
	class TestEvent final : public Core::Event
	{
	public:
		static Core::EventType GetStaticType() { return Core::EventType::None; }
		Core::EventType GetEventType() const override { return GetStaticType(); }
		const char* GetName() const override { return "TestEvent"; }
		Core::EventCategory GetCategoryFlags() const override { return Core::EventCategory::Application; }
	};

	class WindowContext
	{
	public:
		WindowContext()
			: m_Context(ImGui::CreateContext())
		{
			ImGuiIO& imGuiIo = ImGui::GetIO();
			imGuiIo.DisplaySize = ImVec2(800.0f, 600.0f);
			imGuiIo.DeltaTime = 1.0f / 60.0f;
			imGuiIo.IniFilename = nullptr;
			imGuiIo.Fonts->Build();
		}

		~WindowContext() { ImGui::DestroyContext(m_Context); }

		WindowContext(const WindowContext&) = delete;
		WindowContext& operator=(const WindowContext&) = delete;

	private:
		ImGuiContext* m_Context = nullptr;
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

TEST_CASE("Add hands back the panel as the type it was given so its owner need not cast the base pointer down")
{
	Core::PanelManager panels;

	CountingPanel* added = panels.Add(std::make_unique<CountingPanel>("console", "Console"));

	static_assert(std::is_same_v<decltype(panels.Add(std::make_unique<CountingPanel>("x", "X"))), CountingPanel*>);
	REQUIRE(added != nullptr);
	CHECK(panels.Find("console") == added);
}

TEST_CASE("Add rejects a duplicate identifier and keeps the panel already there")
{
	Core::PanelManager panels;
	panels.Add(std::make_unique<CountingPanel>("console", "Console"));

	Core::Panel* duplicate = panels.Add(std::make_unique<CountingPanel>("console", "Other"));

	CHECK(duplicate == nullptr);
	CHECK(panels.GetCount() == 1);

	const Core::Panel* kept = panels.Find("console");
	REQUIRE(kept != nullptr);
	CHECK(kept->GetTitle() == "Console");
}

TEST_CASE("Find returns nullptr for an unknown identifier")
{
	Core::PanelManager panels;

	CHECK(panels.Find("missing") == nullptr);
}

TEST_CASE("SetVisible changes visibility and signals an unknown identifier")
{
	Core::PanelManager panels;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Files"));

	CHECK(panels.SetVisible("files", false));
	CHECK_FALSE(files->IsVisible());
	CHECK_FALSE(panels.SetVisible("missing", true));
}

TEST_CASE("OnUpdate reaches all panels, including hidden ones")
{
	Core::PanelManager panels;
	CountingPanel* visiblePanel = panels.Add(std::make_unique<CountingPanel>("a", "A"));
	CountingPanel* hiddenPanel = panels.Add(std::make_unique<CountingPanel>("b", "B"));
	hiddenPanel->SetVisible(false);

	panels.OnUpdate(kOneFrame);

	CHECK(visiblePanel->updatesReceived == 1);
	CHECK(hiddenPanel->updatesReceived == 1);
}

TEST_CASE("RegisterCommands gives every panel a command in the panels group")
{
	Core::PanelManager panels;
	Core::CommandRegistry commands;
	panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
	panels.Add(std::make_unique<CountingPanel>("console", "Console"));

	panels.RegisterCommands(commands);

	const std::vector<const Core::Command*> toolsCommands = commands.InGroup("panels.tools");

	REQUIRE(toolsCommands.size() == 2);
	CHECK(toolsCommands[0]->Id == "panel.files");
	CHECK(toolsCommands[0]->Label == "Explorer");
	CHECK(toolsCommands[1]->Id == "panel.console");
}

TEST_CASE("A panel's command lands in the menu group the panel names which decides its section of the menu")
{
	Core::PanelManager panels;
	Core::CommandRegistry commands;
	panels.Add(std::make_unique<CountingPanel>("files", "Explorer", "navigate"));
	panels.Add(std::make_unique<CountingPanel>("console", "Console", "tools"));

	panels.RegisterCommands(commands);

	const std::vector<const Core::Command*> navigateCommands = commands.InGroup("panels.navigate");
	const std::vector<const Core::Command*> toolsCommands = commands.InGroup("panels.tools");

	REQUIRE(navigateCommands.size() == 1);
	CHECK(navigateCommands[0]->Id == "panel.files");
	REQUIRE(toolsCommands.size() == 1);
	CHECK(toolsCommands[0]->Id == "panel.console");
}

TEST_CASE("A panel's command opens a hidden panel, asks for focus and reports its state")
{
	Core::PanelManager panels;
	Core::CommandRegistry commands;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer", "tools", StartsAs::Hidden));
	panels.RegisterCommands(commands);

	REQUIRE_FALSE(commands.Find("panel.files")->Checked());

	commands.Run("panel.files");

	CHECK(files->IsVisible());
	CHECK(commands.Find("panel.files")->Checked());
	CHECK(files->ConsumeFocusRequest());
}

TEST_CASE("A panel's command leaves an open panel open and brings it forward rather than toggling so the click meant to find a buried panel does not close it")
{
	Core::PanelManager panels;
	Core::CommandRegistry commands;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
	panels.RegisterCommands(commands);

	REQUIRE(files->IsVisible());

	commands.Run("panel.files");
	commands.Run("panel.files");

	CHECK(files->IsVisible());
	CHECK(files->ConsumeFocusRequest());
}

TEST_CASE("A focus request is consumed once")
{
	CountingPanel files("files", "Explorer");

	CHECK_FALSE(files.ConsumeFocusRequest());

	files.RequestFocus();

	CHECK(files.ConsumeFocusRequest());
	CHECK_FALSE(files.ConsumeFocusRequest());
}

TEST_CASE("A panel added after RegisterCommands still gets its command so OnAttach may add panels and register commands in either order")
{
	Core::PanelManager panels;
	Core::CommandRegistry commands;
	panels.RegisterCommands(commands);

	panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));

	REQUIRE(commands.Find("panel.files") != nullptr);
}

TEST_CASE("An event reaches only the panels that drew so a shortcut capture behind another tab cannot swallow every keystroke")
{
	Core::PanelManager panels;
	ListeningPanel* drawnPanel = panels.Add(std::make_unique<ListeningPanel>("files", EventResponse::PassOn));
	ListeningPanel* hiddenPanel = panels.Add(std::make_unique<ListeningPanel>("keybindings", EventResponse::PassOn));

	drawnPanel->SetDrawnOnLatestFrame(true);
	hiddenPanel->SetDrawnOnLatestFrame(false);

	TestEvent event;
	panels.OnEvent(event);

	CHECK(drawnPanel->eventsReceived == 1);
	CHECK(hiddenPanel->eventsReceived == 0);
}

TEST_CASE("A panel that consumes an event stops it reaching the rest because a panel recording a chord must keep the keystroke from the shortcut router")
{
	Core::PanelManager panels;
	ListeningPanel* consumingPanel = panels.Add(std::make_unique<ListeningPanel>("first", EventResponse::Consume));
	ListeningPanel* laterPanel = panels.Add(std::make_unique<ListeningPanel>("second", EventResponse::PassOn));

	consumingPanel->SetDrawnOnLatestFrame(true);
	laterPanel->SetDrawnOnLatestFrame(true);

	TestEvent event;
	panels.OnEvent(event);

	CHECK(consumingPanel->eventsReceived == 1);
	CHECK(laterPanel->eventsReceived == 0);
	CHECK(event.Handled);
}

TEST_CASE("A panel starts out not drawn because events arrive before the first frame is drawn")
{
	Core::PanelManager panels;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));

	CHECK_FALSE(files->HasBeenDrawnOnLatestFrame());
}

TEST_CASE("ResetVisibility restores each panel's own default so Reset Layout does not open a panel that starts hidden")
{
	Core::PanelManager panels;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
	Core::Panel* keyboardShortcuts = panels.Add(std::make_unique<CountingPanel>("keybindings", "Keyboard Shortcuts", "tools", StartsAs::Hidden));

	REQUIRE(files->IsVisible());
	REQUIRE_FALSE(keyboardShortcuts->IsVisible());

	files->SetVisible(false);
	keyboardShortcuts->SetVisible(true);
	panels.ResetVisibility();

	CHECK(files->IsVisible());
	CHECK_FALSE(keyboardShortcuts->IsVisible());
}

TEST_CASE("A panel's window name shows its icon before the title and keeps its id stable")
{
	CountingPanel consoleWithoutIcon("console", "Console");
	CHECK(consoleWithoutIcon.GetWindowName() == "Console###console");

	IconPanel terminalWithIcon;
	CHECK(terminalWithIcon.GetWindowName() == "*  Terminal###terminal");
}

TEST_CASE("A panel's command carries the panel's icon for the menu to draw")
{
	Core::PanelManager panels;
	Core::CommandRegistry commands;
	panels.Add(std::make_unique<IconPanel>());
	panels.Add(std::make_unique<CountingPanel>("console", "Console"));

	panels.RegisterCommands(commands);

	const std::vector<const Core::Command*> toolsCommands = commands.InGroup("panels.tools");

	REQUIRE(toolsCommands.size() == 2);
	CHECK(toolsCommands[0]->Icon == "*");
	CHECK(toolsCommands[0]->Label == "Terminal");
	CHECK(toolsCommands[1]->Icon.empty());
}

TEST_CASE("A panel's command keeps its short menu label and says what it does in a list")
{
	Core::PanelManager panels;
	Core::CommandRegistry commands;
	panels.Add(std::make_unique<CountingPanel>("search", "Find in Files"));

	panels.RegisterCommands(commands);

	const Core::Command* command = commands.Find("panel.search");

	REQUIRE(command != nullptr);
	CHECK(command->Label == "Find in Files");
	CHECK(command->ListLabel == "Show Find in Files Panel");
}

TEST_CASE("ForEach visits every panel in the order they were added")
{
	Core::PanelManager panels;
	panels.Add(std::make_unique<CountingPanel>("first", "First"));
	panels.Add(std::make_unique<CountingPanel>("second", "Second", "tools", StartsAs::Hidden));

	std::vector<std::string> visitedIds;
	panels.ForEach([&visitedIds](Core::Panel& panel) { visitedIds.push_back(panel.GetId()); });

	CHECK(visitedIds == std::vector<std::string>{"first", "second"});
}

TEST_CASE("Toggle shows a hidden panel and asks for focus")
{
	Core::PanelManager panels;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer", "tools", StartsAs::Hidden));

	CHECK(panels.Toggle("files"));
	CHECK(files->IsVisible());
	CHECK(files->ConsumeFocusRequest());
}

TEST_CASE("Toggle brings forward a visible panel that was not drawn and leaves it visible")
{
	Core::PanelManager panels;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
	files->SetVisible(true);
	files->SetDrawnOnLatestFrame(false);

	CHECK(panels.Toggle("files"));
	CHECK(files->IsVisible());
	CHECK(files->ConsumeFocusRequest());
}

TEST_CASE("Toggle hides a visible panel that was drawn")
{
	Core::PanelManager panels;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));
	files->SetVisible(true);
	files->SetDrawnOnLatestFrame(true);

	CHECK(panels.Toggle("files"));
	CHECK_FALSE(files->IsVisible());
	CHECK_FALSE(files->ConsumeFocusRequest());
}

TEST_CASE("Toggle reports an unknown identifier")
{
	Core::PanelManager panels;

	CHECK_FALSE(panels.Toggle("missing"));
}

TEST_CASE("A panel brought forward from a collapsed floating window is expanded")
{
	const WindowContext context;
	Core::PanelManager panels;
	Core::TabCloseRequests closeRequests;
	Core::Panel* files = panels.Add(std::make_unique<CountingPanel>("files", "Explorer"));

	const auto renderFrame = [&] {
		ImGui::NewFrame();
		panels.OnImGuiRender(closeRequests);
		ImGui::EndFrame();
	};

	for (int frame = 0; frame < 3; ++frame)
	{
		renderFrame();
	}
	REQUIRE(files->HasBeenDrawnOnLatestFrame());

	ImGui::SetWindowCollapsed(files->GetWindowName().c_str(), true);
	renderFrame();
	REQUIRE_FALSE(files->HasBeenDrawnOnLatestFrame());

	CHECK(panels.Toggle("files"));
	renderFrame();
	renderFrame();

	CHECK(files->HasBeenDrawnOnLatestFrame());
}

TEST_CASE("A panel is padded at its sides unless it asks to be flush")
{
	const CountingPanel padded{"padded", "Padded"};
	const CountingPanel flush{"flush", "Flush", "tools", StartsAs::Visible, Sides::Flush};

	CHECK(padded.GetSides() == Sides::Padded);
	CHECK(flush.GetSides() == Sides::Flush);
}

namespace {

	constexpr ImVec2 kInterfacePadding{8.0f, 8.0f};
	constexpr int kDockSettleFrames = 4;

	struct DockedPanels
	{
		ImRect ExplorerTab;
		ImRect ChangesTab;
		ImVec2 ExplorerPadding;
		ImVec2 ChangesPadding;
		float ExplorerContentLeft = 0.0f;
		float ExplorerLeft = 0.0f;
	};

	class DockingContext
	{
	public:
		DockingContext()
			: m_Context(ImGui::CreateContext())
		{
			ImGuiIO& imGuiIo = ImGui::GetIO();
			imGuiIo.DisplaySize = ImVec2(800.0f, 600.0f);
			imGuiIo.DeltaTime = 1.0f / 60.0f;
			imGuiIo.IniFilename = nullptr;
			imGuiIo.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			imGuiIo.Fonts->Build();
			ImGui::GetStyle().WindowPadding = kInterfacePadding;
		}

		~DockingContext() { ImGui::DestroyContext(m_Context); }

		DockingContext(const DockingContext&) = delete;
		DockingContext& operator=(const DockingContext&) = delete;

	private:
		ImGuiContext* m_Context = nullptr;
	};

	// Explorer and Changes docked as tabs of the side column, Explorer flush or padded, drawn by the panel manager.
	DockedPanels DockedBesideEachOther(Sides explorerSides)
	{
		const DockingContext context;
		Core::DockSpaceHost host;
		Core::PanelManager panels;
		Core::TabCloseRequests closeRequests;
		const Core::Panel* explorer =
			panels.Add(std::make_unique<CountingPanel>("files", "Explorer", "tools", StartsAs::Visible, explorerSides));
		const Core::Panel* changes = panels.Add(std::make_unique<CountingPanel>("changes", "Changes"));

		Core::DockLayout layout;
		layout.Left = {explorer->GetWindowName(), changes->GetWindowName()};

		for (int frame = 0; frame < kDockSettleFrames; ++frame)
		{
			ImGui::NewFrame();
			host.Begin(layout);
			host.End();
			panels.OnImGuiRender(closeRequests);
			ImGui::EndFrame();
		}

		const ImGuiWindow* explorerWindow = ImGui::FindWindowByName(explorer->GetWindowName().c_str());
		const ImGuiWindow* changesWindow = ImGui::FindWindowByName(changes->GetWindowName().c_str());
		REQUIRE(explorerWindow != nullptr);
		REQUIRE(changesWindow != nullptr);
		REQUIRE(explorerWindow->DockIsActive);
		REQUIRE(explorerWindow->DockNode == changesWindow->DockNode);

		return DockedPanels{.ExplorerTab = explorerWindow->DC.DockTabItemRect,
							.ChangesTab = changesWindow->DC.DockTabItemRect,
							.ExplorerPadding = explorerWindow->WindowPadding,
							.ChangesPadding = changesWindow->WindowPadding,
							.ExplorerContentLeft = explorerWindow->DC.CursorStartPos.x,
							.ExplorerLeft = explorerWindow->Pos.x};
	}
}

TEST_CASE("A flush panel's window has no padding at its sides and keeps the interface's above and below")
{
	const DockedPanels docked = DockedBesideEachOther(Sides::Flush);

	CHECK(docked.ExplorerPadding.x == doctest::Approx(0.0f));
	CHECK(docked.ExplorerPadding.y == doctest::Approx(kInterfacePadding.y));
	CHECK(docked.ExplorerContentLeft == doctest::Approx(docked.ExplorerLeft));
	CHECK(docked.ChangesPadding.x == doctest::Approx(kInterfacePadding.x));
}

TEST_CASE("A flush panel's dock tab stands where it would were the panel padded, and so does the tab beside it")
{
	const DockedPanels flush = DockedBesideEachOther(Sides::Flush);
	const DockedPanels padded = DockedBesideEachOther(Sides::Padded);

	REQUIRE(padded.ExplorerTab.GetWidth() > 0.0f);
	REQUIRE(padded.ChangesTab.GetWidth() > 0.0f);

	CHECK(flush.ExplorerTab.Min.x == doctest::Approx(padded.ExplorerTab.Min.x));
	CHECK(flush.ExplorerTab.Min.y == doctest::Approx(padded.ExplorerTab.Min.y));
	CHECK(flush.ExplorerTab.Max.x == doctest::Approx(padded.ExplorerTab.Max.x));
	CHECK(flush.ExplorerTab.Max.y == doctest::Approx(padded.ExplorerTab.Max.y));
	CHECK(flush.ChangesTab.Min.x == doctest::Approx(padded.ChangesTab.Min.x));
	CHECK(flush.ChangesTab.Max.x == doctest::Approx(padded.ChangesTab.Max.x));
	CHECK(flush.ChangesTab.Max.y == doctest::Approx(padded.ChangesTab.Max.y));
}
