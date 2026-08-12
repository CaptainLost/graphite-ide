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

	class KeyBindingsPanel final : public Panel
	{
	public:
		KeyBindingsPanel(CommandRegistry& commands, Keymap& keys, std::function<void()> onChanged);

		void OnImGuiRender() override;

		void OnNotDrawn() override;

		bool IsCapturingInput() const override { return !m_Capturing.empty(); }

	private:
		std::optional<Chord> CaptureChord(bool pointerOverCell);

		void RenderRow(const Command& command);

		void RenderSlot(const std::string& id, std::size_t slot, const std::string& text);

		void Offer(const std::string& id, std::size_t slot, const Chord& chord);

		void Apply(const std::string& id, std::size_t slot, const Chord& chord);

		void RenderConflictPopup();

		void ForgetConflict();

		bool Matches(const Command& command) const;

		CommandRegistry& m_Commands;
		Keymap& m_Keys;
		std::function<void()> m_OnChanged;

		std::string m_Capturing;
		std::size_t m_CapturingSlot = 0;

		std::optional<Chord> m_Conflict;
		std::string m_ConflictOwner;

		bool m_ConflictAsked = false;

		std::array<char, 128> m_Filter{};
	};
}