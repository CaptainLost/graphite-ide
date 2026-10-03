#pragma once

#include "Core/Base/KeyCodes.h"
#include "Core/Events/Event.h"

#include <format>
#include <string>

namespace Core {

	class KeyEvent : public Event
	{
	public:
		KeyCode GetKeyCode() const { return m_KeyCode; }
		KeyModifier GetModifiers() const { return m_Modifiers; }

		CORE_EVENT_CLASS_CATEGORY(EventCategory::Keyboard | EventCategory::Input)

	protected:
		KeyEvent(KeyCode keyCode, KeyModifier modifiers) : m_KeyCode(keyCode), m_Modifiers(modifiers) {}

	private:
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

		// Reported rather than filtered: a shortcut usually wants only the first press, an
		// arrow key in the editor wants every one.
		bool IsRepeat() const { return m_IsRepeat; }

		std::string ToString() const override
		{
			return std::format("KeyPressedEvent: {} (repeat={})", static_cast<int>(GetKeyCode()), m_IsRepeat);
		}

		CORE_EVENT_CLASS_TYPE(KeyPressed)

	private:
		bool m_IsRepeat = false;
	};

	class KeyReleasedEvent final : public KeyEvent
	{
	public:
		KeyReleasedEvent(KeyCode keyCode, KeyModifier modifiers) : KeyEvent(keyCode, modifiers) {}

		std::string ToString() const override
		{
			return std::format("KeyReleasedEvent: {}", static_cast<int>(GetKeyCode()));
		}

		CORE_EVENT_CLASS_TYPE(KeyReleased)
	};

	// The character a keystroke produced, after the keyboard layout and any dead keys have had
	// their say. This is what text input must read: a key code cannot tell a comma from a
	// semicolon on a layout nobody anticipated.
	class KeyTypedEvent final : public Event
	{
	public:
		explicit KeyTypedEvent(unsigned int codepoint) : m_Codepoint(codepoint) {}

		unsigned int GetCodepoint() const { return m_Codepoint; }

		std::string ToString() const override { return std::format("KeyTypedEvent: {}", m_Codepoint); }

		CORE_EVENT_CLASS_TYPE(KeyTyped)
		CORE_EVENT_CLASS_CATEGORY(EventCategory::Keyboard | EventCategory::Input)

	private:
		unsigned int m_Codepoint = 0;
	};
}
