#pragma once

#include "Core/Command/Chord.h"
#include "Core/UI/Panel.h"

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

	class KeyBindingsPanel final : public Core::Panel
	{
	public:
		KeyBindingsPanel(Core::CommandRegistry& commands, Core::Keymap& keys, std::function<void()> onChanged);

		void OnImGuiRender() override;

		void OnEvent(Core::Event& event) override;

	private:
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

		// Whether this screen held the focus while it last drew. Events arrive before the
		// frame they belong to, so a capture has to be judged against the frame just past:
		// without it a keystroke aimed at the editor would be recorded by a cell waiting on
		// a panel the user is no longer looking at.
		bool m_WasFocused = false;

		// The modifiers held since the first went down, cleared as soon as anything else is
		// pressed. Non-zero means the release of a modifier still counts as a tap.
		ImGuiKeyChord m_TapMods = 0;

		std::string m_Capturing;
		std::size_t m_CapturingSlot = 0;

		std::optional<Core::Chord> m_Conflict;
		std::string m_ConflictOwner;

		bool m_ConflictAsked = false;

		std::array<char, 128> m_Filter{};
	};
}