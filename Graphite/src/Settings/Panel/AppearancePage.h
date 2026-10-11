#pragma once

#include "Settings/Panel/SettingsPage.h"
#include "Theme/SchemeDraft.h"
#include "Theme/SchemeStore.h"
#include "UI/ModalQuestion.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Core {

	class EventBus;
}

namespace Graphite {

	class ActiveScheme;
	class AppSettings;

	// Chooses the colour scheme and the interface scale, and edits the colours of a scheme of the
	// user's own. Holds a SchemeDraft for every decision about schemes and keeps only what ImGui needs
	// on top of it.
	class AppearancePage final : public SettingsPage
	{
	public:
		AppearancePage(ActiveScheme& scheme, AppSettings& settings, ModalGate& modals, Core::EventBus& events,
					   std::filesystem::path themesDirectory);

		std::string_view GetId() const override { return "appearance"; }

		std::string_view GetTitle() const override { return "Appearance"; }

		const char* GetIcon() const override;

		std::vector<SettingsCard> Describe() override;

		// A colour picker lives in a popup, so the swatch never reports an edit as finished the way an
		// inline field would; the edit is saved once the mouse is released.
		void Update() override;

		void RenderDialogs() override;

		// Re-resolves from the saved setting, so a scheme abandoned part-way cannot leave the editor
		// painted from something that was never written down.
		void OnDeactivated() override;

	private:
		SettingsCard DescribeTheme();
		SettingsCard DescribeInterface();

		// Templated over the enum so every group of colours shares one grid, rather than a copy per
		// group drifting apart.
		template<typename Which>
			requires std::is_enum_v<Which>
		SettingsCard DescribeColours(const char* heading, std::size_t count);

		template<typename Which>
			requires std::is_enum_v<Which>
		void DrawColourGrid(std::size_t count);

		template<typename Which>
			requires std::is_enum_v<Which>
		void DrawColourSwatch(Which which);

		template<typename Which>
			requires std::is_enum_v<Which>
		void DrawColourReset(Which which);

		template<typename Which>
			requires std::is_enum_v<Which>
		void DrawColour(Which which);

		template<typename Which>
			requires std::is_enum_v<Which>
		void DrawColourResult(Which which);

		template<typename Which>
			requires std::is_enum_v<Which>
		std::vector<SettingsRow> FindColours(std::size_t count, std::span<const std::string> words);

		std::vector<std::string> SchemeActionLabels() const;

		void DrawSchemeChoice();
		void DrawSchemeActions();
		void DrawInterfaceScale();

		void Apply();
		ImVec4 DangerColour() const;
		void RenderRenameDialog();
		void RenderDeleteDialog();
		void FlushColourEdit();

		// Raises a toast for each save, duplicate or delete of the draft's that failed, in order. The
		// draft queues them rather than holding the bus, so it can be tested without one.
		void PublishDraftFailure();

		ActiveScheme& m_Active;
		AppSettings& m_Settings;
		Core::EventBus& m_Events;

		SchemeDraft m_Draft;

		ModalQuestion<std::string> m_DeleteQuestion;

		ModalQuestion<std::string> m_RenameQuestion;

		// The rename field's text, loaded from the payload on the first frame the dialog shows. Room for
		// the longest name allowed in four-byte characters, and the terminator.
		std::array<char, kLongestSchemeName * 4 + 1> m_RenameBuffer{};
		bool m_RenameShown = false;
		std::string m_RenameError;

		bool m_ColourEdited = false;
	};
}
