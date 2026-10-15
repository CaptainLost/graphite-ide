#pragma once

#include "Core/Command/Chord.h"
#include "Theme/ActiveScheme.h"
#include "UI/Dialogs/ModalQuestion.h"
#include "Settings/Panel/SettingsPage.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	class CommandRegistry;
	class Keymap;
	struct Command;
}

namespace Graphite {

	class KeyBindingsPage final : public SettingsPage
	{
	public:
		KeyBindingsPage(Core::CommandRegistry& commands, Core::Keymap& keys, ModalGate& modals, const ActiveScheme& scheme,
						std::function<void()> onChanged);

		std::string_view GetId() const override { return "keyBindings"; }

		std::string_view GetTitle() const override { return "Key Bindings"; }

		const char* GetIcon() const override;

		std::vector<SettingsCard> Describe() override;

		void RenderDialogs() override;

		void OnEvent(Core::Event& event) override;

		void OnDeactivated() override;

		void OnOpenedFromSearch(std::string_view query) override;

		const std::string& GetFilter() const { return m_Filter; }

		std::vector<SettingsRow> FindCommands(std::span<const std::string> words) const;

	private:
		struct ContestedChord
		{
			Core::Chord Chord;
			std::string OwnerCommandId;
			std::string Reason;
			std::string CommandId;
			std::size_t SlotIndex = 0;

			bool operator==(const ContestedChord&) const = default;
		};

		bool IsWaitingForChord() const;

		void CancelCapture();

		static bool CaptureOwnsKeyboard(bool cellIsActive, bool anyItemIsActive);

		std::optional<Core::Chord> CaptureWheelChord(bool pointerOverCell);

		void RenderRow(const Core::Command& command);

		void RenderSlot(const std::string& commandId, std::size_t slotIndex, const std::string& chordText);

		void Offer(const std::string& commandId, std::size_t slotIndex, const Core::Chord& chord);

		void Apply(const std::string& commandId, std::size_t slotIndex, const Core::Chord& chord);

		void AskConflict(ContestedChord conflict);

		void RenderConflictQuestion();

		void RenderRestoreQuestion();

		void TakeFromOwner(const ContestedChord& conflict);

		void DrawRestoreAllDefaults();

		void DrawShortcuts();

		bool CommandMatches(const Core::Command& command, std::span<const std::string> words) const;

		void AppendIfMatching(const Core::Command& command, std::span<const std::string> words, std::vector<SettingsRow>& rows) const;

		Core::CommandRegistry& m_Commands;
		Core::Keymap& m_Keys;
		const ActiveScheme& m_Scheme;
		std::function<void()> m_OnChanged;

		bool m_WasFocusedLastFrame = false;

		ImGuiKeyChord m_PendingTapModifiers = 0;

		std::string m_CapturingCommandId;
		std::size_t m_CapturingSlotIndex = 0;

		ModalQuestion<ContestedChord> m_ConflictQuestion;

		ModalQuestion<bool> m_RestoreQuestion;

		bool m_IsConflictAwaitingAnswer = false;

		std::string m_Filter;
	};
}
