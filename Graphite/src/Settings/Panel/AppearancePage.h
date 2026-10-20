#pragma once

#include "Settings/Panel/SettingsPage.h"
#include "Theme/SchemeDraft.h"
#include "Theme/SchemeStore.h"
#include "UI/Dialogs/ModalQuestion.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
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

	class AppearancePage final : public SettingsPage
	{
	public:
		AppearancePage(ActiveScheme& scheme, AppSettings& settings, ModalGate& modals, Core::EventBus& events,
					   std::filesystem::path themesDirectory);

		std::string_view GetId() const override { return "appearance"; }

		std::string_view GetTitle() const override { return "Appearance"; }

		const char* GetIcon() const override;

		std::vector<SettingsCard> Describe() override;

		// WARNING: A colour picker lives in a popup, so the swatch never reports an edit as finished the way an
		// inline field would; the edit is saved once the mouse is released.
		void Update() override;

		void RenderDialogs() override;

		void OnDeactivated() override;

		void OnOpenedFromSearch(std::string_view openWith) override;

	private:
		struct RevealedColour
		{
			std::string OpenWith;
			int ScrollsLeft = 0;
			float SecondsShown = 0.0f;
		};

		SettingsCard DescribeTheme();
		SettingsCard DescribeInterface();
		SettingsCard DescribeLayout();

		void DrawLayoutMetric(LayoutMetric metric);
		bool ShowsLayoutReset(LayoutMetric metric) const;

		template<typename Which>
			requires std::is_enum_v<Which>
		SettingsCard DescribeColours(const char* heading, std::size_t count);

		template<typename Which>
			requires std::is_enum_v<Which>
		void DrawColourGrid(const char* heading, std::size_t count);

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
		bool ShowsReset(Which which) const;

		template<typename Which>
			requires std::is_enum_v<Which>
		void DrawColourResult(Which which);

		template<typename Which>
			requires std::is_enum_v<Which>
		std::vector<SettingsRow> FindColours(const char* heading, std::size_t count, std::span<const std::string> words);

		bool IsRevealed(std::string_view heading, std::string_view name) const;
		bool IsScrollingToGroup(std::string_view heading) const;
		ImU32 RevealHighlight() const;
		void ScrollToRevealedColour();
		void AdvanceReveal();

		std::vector<std::string> SchemeActionLabels() const;

		void DrawSchemeChoice();
		void DrawSchemeActions();
		void DrawThemesFolder();
		void DrawInterfaceScale();

		void Apply();
		void RenderRenameDialog();
		void RenderDeleteDialog();
		void FlushColourEdit();

		void PublishDraftFailure();

		ActiveScheme& m_Active;
		AppSettings& m_Settings;
		Core::EventBus& m_Events;

		std::filesystem::path m_ThemesDirectory;

		SchemeDraft m_Draft;

		ModalQuestion<std::string> m_DeleteQuestion;

		ModalQuestion<std::string> m_RenameQuestion;

		static constexpr std::size_t kLongestCharacterBytes = 4;
		static constexpr std::size_t kTerminatorBytes = 1;

		std::array<char, kLongestSchemeName * kLongestCharacterBytes + kTerminatorBytes> m_RenameBuffer{};
		bool m_RenameShown = false;
		std::string m_RenameError;

		bool m_ColourEdited = false;

		std::optional<RevealedColour> m_Revealed;
	};
}
