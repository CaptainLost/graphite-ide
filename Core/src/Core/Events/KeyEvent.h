#pragma once

#include "Core/Base/KeyCodes.h"
#include "Core/Events/Event.h"

#include <format>

namespace Core {

	class KeyEvent : public Event
	{
	public:
		KeyCode GetKeyCode() const { return m_KeyCode; }
		KeyModifier GetModifiers() const { return m_Modifiers; }

		EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	protected:
		KeyEvent(KeyCode keyCode, KeyModifier modifiers) : m_KeyCode(keyCode), m_Modifiers(modifiers) {}

		KeyCode m_KeyCode = KeyCode::Space;
		KeyModifier m_Modifiers = KeyModifier::None;
	};

	class KeyPressedEvent final : public KeyEvent
	{
	public:
		KeyPressedEvent(KeyCode keyCode, KeyModifier modifiers, bool isRepeat)
			: KeyEvent(keyCode, modifiers), m_IsRepeat(isRepeat)
		{
		}

		// A held key repeats. A shortcut usually wants only the first press; an arrow key in
		// the editor wants every one, which is why this is reported rather than filtered.
		bool IsRepeat() const { return m_IsRepeat; }

		std::string ToString() const override
		{
			return std::format("KeyPressedEvent: {} (repeat={})", static_cast<int>(m_KeyCode), m_IsRepeat);
		}

		EVENT_CLASS_TYPE(KeyPressed)

	private:
		bool m_IsRepeat = false;
	};

	class KeyReleasedEvent final : public KeyEvent
	{
	public:
		KeyReleasedEvent(KeyCode keyCode, KeyModifier modifiers) : KeyEvent(keyCode, modifiers) {}

		std::string ToString() const override
		{
			return std::format("KeyReleasedEvent: {}", static_cast<int>(m_KeyCode));
		}

		EVENT_CLASS_TYPE(KeyReleased)
	};

	// The character a keystroke produced, after the keyboard layout and any dead keys have
	// had their say. This is what text input must read: a key code cannot tell you whether
	// the user typed a comma or a semicolon on a layout you did not anticipate.
	class KeyTypedEvent final : public Event
	{
	public:
		explicit KeyTypedEvent(unsigned int codepoint) : m_Codepoint(codepoint) {}

		unsigned int GetCodepoint() const { return m_Codepoint; }

		std::string ToString() const override { return std::format("KeyTypedEvent: {}", m_Codepoint); }

		EVENT_CLASS_TYPE(KeyTyped)
		EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	private:
		unsigned int m_Codepoint = 0;
	};
}
