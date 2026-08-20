#pragma once

#include "Core/Base/Base.h"

#include <string>

namespace Core {

	// Dispatched the moment they arrive, not queued: a handler that consumes one has to do so
	// before the frame that produced it draws, or the shortcut and the text field both see the
	// same key.
	enum class EventType
	{
		None = 0,
		WindowClose,
		WindowResize,
		WindowFocus,
		WindowLostFocus,
		WindowMoved,
		FileDrop,
		KeyPressed,
		KeyReleased,
		KeyTyped,
		MouseButtonPressed,
		MouseButtonReleased,
		MouseMoved,
		MouseScrolled
	};

	// Lets a handler consume a whole class of event without naming every type in it, which is
	// what the GUI layer does when it has the keyboard.
	enum EventCategory
	{
		EventCategoryNone = 0,
		EventCategoryApplication = BIT(0),
		EventCategoryInput = BIT(1),
		EventCategoryKeyboard = BIT(2),
		EventCategoryMouse = BIT(3),
		EventCategoryMouseButton = BIT(4)
	};

// clang-format off
#define EVENT_CLASS_TYPE(type)                                             \
	static EventType GetStaticType() { return EventType::type; }           \
	virtual EventType GetEventType() const override { return GetStaticType(); } \
	virtual const char* GetName() const override { return #type; }

#define EVENT_CLASS_CATEGORY(category) \
	virtual int GetCategoryFlags() const override { return category; }
// clang-format on

	class Event
	{
	public:
		virtual ~Event() = default;

		// Set by whoever deals with the event. Application stops walking the layer stack as
		// soon as it is true, so the topmost interested layer wins.
		bool Handled = false;

		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;
		virtual int GetCategoryFlags() const = 0;
		virtual std::string ToString() const { return GetName(); }

		bool IsInCategory(EventCategory category) const { return (GetCategoryFlags() & category) != 0; }
	};

	class EventDispatcher
	{
	public:
		explicit EventDispatcher(Event& event) : m_Event(event) {}

		// Runs the handler only when the event is of the handler's type. Its answer is ORed
		// into Handled rather than assigned: an event consumed upstream stays consumed.
		template<typename T, typename F>
		bool Dispatch(const F& handler)
		{
			if (m_Event.GetEventType() != T::GetStaticType())
				return false;

			m_Event.Handled |= handler(static_cast<T&>(m_Event));
			return true;
		}

	private:
		Event& m_Event;
	};
}
