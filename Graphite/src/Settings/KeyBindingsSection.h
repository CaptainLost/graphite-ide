#pragma once

#include "Core/Command/Chord.h"
#include "UI/ModalQuestion.h"
#include "Settings/SettingsSection.h"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	class CommandRegistry;
	class Keymap;
	struct Command;
}

namespace Graphite {

	class KeyBindingsSection final : public SettingsSection
	{
	public:
		KeyBindingsSection(Core::CommandRegistry& commands, Core::Keymap& keys, ModalGate& modals,
						   std::function<void()> onChanged);

		std::string_view GetTitle() const override { return "Key Bindings"; }

		void OnImGuiRender() override;

		void OnEvent(Core::Event& event) override;

		// The question's popup is torn down with the section that renders it, and only this
		// section's render can release the gate — so it has to be given back here.
		void OnDeactivated() override;

	private:
		// One chord the user tried to bind and could not simply have, with the cell it was meant
		// for. The owner is empty when the chord is one the toolkit keeps for itself, which is a
		// refusal rather than a question: there is nobody to take it from, and Reason says why.
		struct Conflict
		{
			Core::Chord Chord;
			std::string Owner;
			std::string Reason;
			std::string Id;
			std::size_t Slot = 0;

			bool operator==(const Conflict&) const = default;
		};

		bool IsWaitingForChord() const;

		void CancelCapture();

		static bool CaptureOwnsKeyboard(bool selfActive, bool anyActive);

		std::optional<Core::Chord> CaptureWheelChord(bool pointerOverCell);

		void RenderRow(const Core::Command& command);

		void RenderSlot(const std::string& id, std::size_t slot, const std::string& text);

		void Offer(const std::string& id, std::size_t slot, const Core::Chord& chord);

		void Apply(const std::string& id, std::size_t slot, const Core::Chord& chord);

		void AskConflict(Conflict conflict);

		void RenderConflictQuestion();

		void RenderRestoreQuestion();

		void TakeFromOwner(const Conflict& conflict);

		bool Matches(const Core::Command& command) const;

		Core::CommandRegistry& m_Commands;
		Core::Keymap& m_Keys;
		std::function<void()> m_OnChanged;

		// Events arrive before the frame they belong to, so a capture is judged against the
		// focus of the frame just past.
		bool m_WasFocused = false;

		// The modifiers held since the first went down, cleared as soon as anything else is
		// pressed. Non-zero means a modifier release still counts as a tap.
		ImGuiKeyChord m_TapMods = 0;

		std::string m_Capturing;
		std::size_t m_CapturingSlot = 0;

		ModalQuestion<Conflict> m_ConflictQuestion;

		// Whether to put every binding back at once. The payload says nothing: there is only one
		// such question.
		ModalQuestion<bool> m_RestoreQuestion;

		// True from the moment a conflict is asked until the capture it belonged to is dealt
		// with. A question that vanishes without an answer — Escape, or the panel hidden — is a
		// "no", and the capture it interrupted must not resume.
		bool m_ConflictPending = false;

		std::array<char, 128> m_Filter{};
	};
}