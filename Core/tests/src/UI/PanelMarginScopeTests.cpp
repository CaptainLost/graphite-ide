#include <doctest/doctest.h>

#include "Core/ImGui/DockSpaceHost.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/UI/Panel.h"
#include "Core/UI/PanelManager.h"
#include "Core/UI/PanelMarginScope.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>
#include <memory>
#include <string>
#include <utility>

namespace {

	constexpr float kInterfacePadding = 8.0f;
	constexpr ImVec2 kWindowSize{300.0f, 200.0f};
	constexpr float kNoSidePadding = 0.0f;

	struct Laid
	{
		float WindowLeft = 0.0f;
		float WindowRight = 0.0f;
		float CursorLeft = 0.0f;
		float RoomLeft = 0.0f;
		float ItemRight = 0.0f;
		float CursorLeftAfter = 0.0f;
		float RoomLeftAfter = 0.0f;
	};

	// A window padded as the interface is, or with no padding at its sides as a flush panel's is, holding one item as
	// wide as the line inside a margin and one more after it.
	class MarginWindow
	{
	public:
		MarginWindow()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& inputOutput = ImGui::GetIO();
			inputOutput.DisplaySize = ImVec2(800.0f, 600.0f);
			inputOutput.DeltaTime = 1.0f / 60.0f;
			inputOutput.IniFilename = nullptr;
			inputOutput.Fonts->Build();

			ImGui::GetStyle().WindowPadding = ImVec2(kInterfacePadding, kInterfacePadding);
		}

		~MarginWindow() { ImGui::DestroyContext(m_Context); }

		MarginWindow(const MarginWindow&) = delete;
		MarginWindow& operator=(const MarginWindow&) = delete;

		Laid Draw(float sidePadding)
		{
			Laid laid;

			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(sidePadding, kInterfacePadding));
			ImGui::Begin("Margins", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			ImGui::PopStyleVar();

			laid.WindowLeft = ImGui::GetWindowPos().x;
			laid.WindowRight = ImGui::GetWindowPos().x + ImGui::GetWindowWidth();

			{
				const Core::PanelMarginScope margin;
				laid.CursorLeft = ImGui::GetCursorScreenPos().x;
				laid.RoomLeft = ImGui::GetContentRegionAvail().x;
				ImGui::Button("wide", ImVec2(-FLT_MIN, 0.0f));
				laid.ItemRight = ImGui::GetItemRectMax().x;
			}

			laid.CursorLeftAfter = ImGui::GetCursorScreenPos().x;
			laid.RoomLeftAfter = ImGui::GetContentRegionAvail().x;
			ImGui::Dummy(ImVec2(1.0f, 1.0f));

			ImGui::End();
			ImGui::EndFrame();

			return laid;
		}

		Laid DrawInAChild()
		{
			Laid laid;

			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::Begin("Margins", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			ImGui::BeginChild("##list", ImVec2(0.0f, 0.0f));

			laid.WindowLeft = ImGui::GetWindowPos().x;
			laid.WindowRight = ImGui::GetWindowPos().x + ImGui::GetWindowWidth();

			{
				const Core::PanelMarginScope margin;
				laid.CursorLeft = ImGui::GetCursorScreenPos().x;
				laid.RoomLeft = ImGui::GetContentRegionAvail().x;
				ImGui::Button("wide", ImVec2(-FLT_MIN, 0.0f));
				laid.ItemRight = ImGui::GetItemRectMax().x;
			}

			ImGui::EndChild();
			ImGui::End();
			ImGui::EndFrame();

			return laid;
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};
}

TEST_CASE("A panel margin stands what is laid out inside it the interface's padding in from a flush window's sides")
{
	MarginWindow window;

	const Laid laid = window.Draw(kNoSidePadding);

	CHECK(laid.CursorLeft == doctest::Approx(laid.WindowLeft + kInterfacePadding));
	CHECK(laid.RoomLeft == doctest::Approx(kWindowSize.x - kInterfacePadding * 2.0f));
	CHECK(laid.ItemRight == doctest::Approx(laid.WindowRight - kInterfacePadding));
}

TEST_CASE("A panel margin gives a flush window its whole width back when it ends")
{
	MarginWindow window;

	const Laid laid = window.Draw(kNoSidePadding);

	CHECK(laid.CursorLeftAfter == doctest::Approx(laid.WindowLeft));
	CHECK(laid.RoomLeftAfter == doctest::Approx(kWindowSize.x));
}

TEST_CASE("A panel margin changes nothing in a window padded as the interface is")
{
	MarginWindow window;

	const Laid laid = window.Draw(kInterfacePadding);

	CHECK(laid.CursorLeft == doctest::Approx(laid.WindowLeft + kInterfacePadding));
	CHECK(laid.RoomLeft == doctest::Approx(kWindowSize.x - kInterfacePadding * 2.0f));
	CHECK(laid.CursorLeftAfter == doctest::Approx(laid.WindowLeft + kInterfacePadding));
}

TEST_CASE("A panel margin changes nothing in a child window, whose content reaches its edges on purpose")
{
	MarginWindow window;

	const Laid laid = window.DrawInAChild();

	CHECK(laid.CursorLeft == doctest::Approx(laid.WindowLeft));
	CHECK(laid.ItemRight == doctest::Approx(laid.WindowRight));
}

namespace {

	constexpr int kDockSettleFrames = 4;

	// A panel that measures, inside a margin, where its first item would stand and how much of the line is left.
	class MeasuringPanel final : public Core::Panel
	{
	public:
		MeasuringPanel(std::string id, Sides sides)
			: Core::Panel(std::move(id), "Measured", "tools", "", StartsAs::Visible, sides)
		{
		}

		void OnImGuiRender() override
		{
			WindowLeft = ImGui::GetWindowPos().x;
			WindowWidth = ImGui::GetWindowWidth();
			Docked = ImGui::IsWindowDocked();

			const Core::PanelMarginScope margin;
			CursorLeft = ImGui::GetCursorScreenPos().x;
			RoomLeft = ImGui::GetContentRegionAvail().x;
		}

		float WindowLeft = 0.0f;
		float WindowWidth = 0.0f;
		float CursorLeft = 0.0f;
		float RoomLeft = 0.0f;
		bool Docked = false;
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
			ImGui::GetStyle().WindowPadding = ImVec2(kInterfacePadding, kInterfacePadding);
		}

		~DockingContext() { ImGui::DestroyContext(m_Context); }

		DockingContext(const DockingContext&) = delete;
		DockingContext& operator=(const DockingContext&) = delete;

	private:
		ImGuiContext* m_Context = nullptr;
	};
}

TEST_CASE("A panel margin stands what is laid out inside it the interface's padding in from a docked flush panel's sides")
{
	using Sides = Core::Panel::Sides;

	const DockingContext context;
	Core::DockSpaceHost host;
	Core::PanelManager panels;
	Core::TabCloseRequests closeRequests;
	const MeasuringPanel* flush = panels.Add(std::make_unique<MeasuringPanel>("flush", Sides::Flush));
	const MeasuringPanel* padded = panels.Add(std::make_unique<MeasuringPanel>("padded", Sides::Padded));

	Core::DockLayout layout;
	layout.Left = {flush->GetWindowName()};
	layout.Bottom = {padded->GetWindowName()};

	for (int frame = 0; frame < kDockSettleFrames; ++frame)
	{
		ImGui::NewFrame();
		host.Begin(layout);
		host.End();
		panels.OnImGuiRender(closeRequests);
		ImGui::EndFrame();
	}

	REQUIRE(flush->Docked);
	REQUIRE(padded->Docked);

	CHECK(flush->CursorLeft == doctest::Approx(flush->WindowLeft + kInterfacePadding));
	CHECK(flush->RoomLeft == doctest::Approx(flush->WindowWidth - kInterfacePadding * 2.0f));
	CHECK(padded->CursorLeft == doctest::Approx(padded->WindowLeft + kInterfacePadding));
	CHECK(padded->RoomLeft == doctest::Approx(padded->WindowWidth - kInterfacePadding * 2.0f));
}
