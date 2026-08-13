#pragma once

#include "Core/Command/Chord.h"
#include "Settings/SettingsSection.h"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace Core {

	class CommandRegistry;
	class Keymap;
	struct Command;
}

namespace Graphite {

	class KeyBindingsSection final : public SettingsSection
	{
	public:
		KeyBindingsSection(Core::CommandRegistry& commands, Core::Keymap& keys, std::function<void()> onChanged);

		std::string_view GetTitle() const override { return "Key Bindings"; }

		void OnImGuiRender() override;

		void OnEvent(Core::Event& event) override;

		void OnDeactivated() override { CancelCapture(); }

	private:
		bool IsWaitingForChord() const;

		void CancelCapture();

		static bool CaptureOwnsKeyboard(bool selfActive, bool anyActive);

		std::optional<Core::Chord> CaptureWheelChord(bool pointerOverCell);

		void RenderRow(const Core::Command& command);

		void RenderSlot(const std::string& id, std::size_t slot, const std::string& text);

		void Offer(const std::string& id, std::size_t slot, const Core::Chord& chord);

		void Apply(const std::string& id, std::size_t slot, const Core::Chord& chord);

		void RenderConflictPopup();

		void ForgetConflict();

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

		std::optional<Core::Chord> m_Conflict;
		std::string m_ConflictOwner;

		// Set instead of the owner when the chord is one the toolkit keeps for itself, which
		// is a refusal rather than a question: there is nobody to take it from.
		std::string m_ConflictReason;

		bool m_ConflictAsked = false;

		std::array<char, 128> m_Filter{};
	};
}