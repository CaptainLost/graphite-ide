#include <doctest/doctest.h>

#include "Core/UI/PanelMarginScope.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>

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
