#include <doctest/doctest.h>

#include "UI/Dialogs/FileDialogOffer.h"

#include "ImGuiFrames.h"

#include "Core/Platform/AsyncDialog.h"

#include <imgui.h>

#include <filesystem>
#include <future>
#include <string_view>

namespace {

	const ImVec2 kButtonTopLeft(20.0f, 20.0f);
	const ImVec2 kButtonSize(120.0f, 30.0f);
	const ImVec2 kIntoTheButton(60.0f, 35.0f);

	void IgnoreFailure(const Core::Error&) {}

	void IgnorePick(const std::filesystem::path&) {}

	// WARNING: The picker waits on a future its test lets go of last, so the dialog stays open for the whole test and its
	// thread still ends.
	class HeldOpenDialog
	{
	public:
		HeldOpenDialog()
		{
			std::shared_future<void> release = m_Release.get_future().share();
			REQUIRE(Dialog.Start(
				[release]() -> Core::Result<std::filesystem::path> {
					release.wait();
					return std::filesystem::path{};
				},
				IgnorePick, IgnoreFailure));
		}

		~HeldOpenDialog() { m_Release.set_value(); }

		HeldOpenDialog(const HeldOpenDialog&) = delete;
		HeldOpenDialog& operator=(const HeldOpenDialog&) = delete;

		Core::AsyncDialog Dialog;

	private:
		std::promise<void> m_Release;
	};

	class OfferScreen
	{
	public:
		explicit OfferScreen(const Core::AsyncDialog& dialog) : m_Dialog(dialog) {}

		bool ClickTheButton()
		{
			bool broughtForward = false;
			m_Frames.Click(kIntoTheButton, [this, &broughtForward] { broughtForward = Draw() || broughtForward; });
			return broughtForward;
		}

	private:
		bool Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f));
			ImGui::Begin("##fileDialogOffer", nullptr,
						 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
							 ImGuiWindowFlags_NoSavedSettings);
			ImGui::SetCursorScreenPos(kButtonTopLeft);
			ImGui::BeginDisabled(m_Dialog.IsOpen());
			ImGui::Button("Open Folder", kButtonSize);
			ImGui::EndDisabled();

			const bool broughtForward = Graphite::OfferOpenFileDialog(m_Dialog);

			ImGui::End();
			return broughtForward;
		}

		ImGuiFrames m_Frames;
		const Core::AsyncDialog& m_Dialog;
	};
}

TEST_CASE("A click on an item disabled while a file dialog is open brings the dialog to the front")
{
	const HeldOpenDialog held;
	OfferScreen screen{held.Dialog};

	CHECK(screen.ClickTheButton());
	CHECK(held.Dialog.IsOpen());
}

TEST_CASE("With no file dialog open an item offers nothing to bring to the front")
{
	const Core::AsyncDialog closed;
	OfferScreen screen{closed};

	CHECK_FALSE(screen.ClickTheButton());
}

TEST_CASE("The note on an item disabled for a file dialog says how to reach the dialog")
{
	CHECK(std::string_view{Graphite::kFileDialogOpenNote} ==
		  "A file dialog is open \xE2\x80\x94 click to bring it to the front");
}
