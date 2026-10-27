#include <doctest/doctest.h>

#include "Core/ImGui/DisabledOffer.h"
#include "Core/ImGui/EmptyState.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>

namespace {

	constexpr const char* kNote = "Busy elsewhere";
	constexpr const char* kWindowName = "##disabledOfferTest";
	const ImVec2 kButtonCentre(60.0f, 40.0f);
	const ImVec2 kWindowSize(400.0f, 300.0f);
	const ImVec2 kButtonSize(100.0f, 40.0f);
	constexpr int kFramesForTooltipDelay = 60;

	struct ButtonLook
	{
		bool Disabled = true;
		bool Offers = true;
	};

	class OfferScreen
	{
	public:
		OfferScreen() : m_Context(ImGui::CreateContext())
		{
			ImGuiIO& inputOutput = ImGui::GetIO();
			inputOutput.DisplaySize = ImVec2(800.0f, 600.0f);
			inputOutput.DeltaTime = 1.0f / 60.0f;
			inputOutput.IniFilename = nullptr;
			inputOutput.Fonts->Build();
		}

		~OfferScreen() { ImGui::DestroyContext(m_Context); }

		OfferScreen(const OfferScreen&) = delete;
		OfferScreen& operator=(const OfferScreen&) = delete;

		void Frame(const std::function<void()>& drawInWindow)
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::Begin(kWindowName, nullptr,
						 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
							 ImGuiWindowFlags_NoSavedSettings);
			drawInWindow();
			ImGui::End();
			ImGui::EndFrame();
		}

		bool ButtonFrame(const ButtonLook& look)
		{
			bool offered = false;

			Frame([&] {
				ImGui::SetCursorScreenPos(ImVec2(kButtonCentre.x - kButtonSize.x * 0.5f, kButtonCentre.y - kButtonSize.y * 0.5f));
				ImGui::BeginDisabled(look.Disabled);
				ImGui::Button("Open", kButtonSize);
				ImGui::EndDisabled();

				if (look.Offers)
				{
					offered = Core::OfferWhileDisabled(kNote);
				}
			});

			return offered;
		}

		bool ClickButton(const ButtonLook& look)
		{
			ImGuiIO& inputOutput = ImGui::GetIO();
			bool offered = false;

			inputOutput.AddMousePosEvent(kButtonCentre.x, kButtonCentre.y);
			offered = ButtonFrame(look) || offered;
			inputOutput.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			offered = ButtonFrame(look) || offered;
			inputOutput.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			offered = ButtonFrame(look) || offered;

			return offered;
		}

		static bool IsTooltipShown()
		{
			for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
			{
				const bool isTooltip = (window->Flags & ImGuiWindowFlags_Tooltip) != 0;

				if (isTooltip && window->Active)
				{
					return true;
				}
			}

			return false;
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};
}

TEST_CASE("A disabled item that offers something answers a click on it all the same")
{
	OfferScreen screen;

	CHECK(screen.ClickButton(ButtonLook{.Disabled = true}));
}

TEST_CASE("A disabled item that offers something says why in a tooltip while the pointer rests on it")
{
	OfferScreen screen;
	ImGui::GetIO().AddMousePosEvent(kButtonCentre.x, kButtonCentre.y);

	for (int frameIndex = 0; frameIndex < kFramesForTooltipDelay; ++frameIndex)
	{
		static_cast<void>(screen.ButtonFrame(ButtonLook{.Disabled = true}));
	}

	CHECK(OfferScreen::IsTooltipShown());
}

TEST_CASE("An item that is not disabled offers nothing and shows no tooltip")
{
	OfferScreen screen;

	CHECK_FALSE(screen.ClickButton(ButtonLook{.Disabled = false}));

	ImGui::GetIO().AddMousePosEvent(kButtonCentre.x, kButtonCentre.y);

	for (int frameIndex = 0; frameIndex < kFramesForTooltipDelay; ++frameIndex)
	{
		static_cast<void>(screen.ButtonFrame(ButtonLook{.Disabled = false}));
	}

	CHECK_FALSE(OfferScreen::IsTooltipShown());
}

TEST_CASE("An empty state's disabled button with a note answers a click, and without one does not")
{
	OfferScreen screen;
	ImGuiIO& inputOutput = ImGui::GetIO();
	const ImVec2 middleOfTheWindow(kWindowSize.x * 0.5f, kWindowSize.y * 0.5f);

	const auto clickInTheMiddle = [&](const char* note) {
		bool clicked = false;
		const auto draw = [&] {
			ImGui::BeginDisabled();
			clicked = Core::EmptyState("", "Open Folder", ImVec2(0.0f, 0.0f), note) || clicked;
			ImGui::EndDisabled();
		};

		inputOutput.AddMousePosEvent(middleOfTheWindow.x, middleOfTheWindow.y);
		screen.Frame(draw);
		inputOutput.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		screen.Frame(draw);
		inputOutput.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		screen.Frame(draw);

		return clicked;
	};

	CHECK(clickInTheMiddle(kNote));
	CHECK_FALSE(clickInTheMiddle(nullptr));
}
