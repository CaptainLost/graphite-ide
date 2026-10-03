#pragma once

#include "Core/Base/Base.h"

#include <concepts>
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
	enum class EventCategory : int
	{
		None = 0,
		Application = 1 << 0,
		Input = 1 << 1,
		Keyboard = 1 << 2,
		Mouse = 1 << 3,
		MouseButton = 1 << 4
	};

	constexpr EventCategory operator|(EventCategory left, EventCategory right)
	{
		return static_cast<EventCategory>(static_cast<int>(left) | static_cast<int>(right));
	}

	constexpr EventCategory operator&(EventCategory left, EventCategory right)
	{
		return static_cast<EventCategory>(static_cast<int>(left) & static_cast<int>(right));
	}

	// clang-format off
#define CORE_EVENT_CLASS_TYPE(type)                                \
	static EventType GetStaticType() { return EventType::type; }       \
	EventType GetEventType() const override { return GetStaticType(); } \
	const char* GetName() const override { return #type; }

#define CORE_EVENT_CLASS_CATEGORY(category) \
	EventCategory GetCategoryFlags() const override { return category; }
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
		virtual EventCategory GetCategoryFlags() const = 0;
		virtual std::string ToString() const { return GetName(); }

		bool IsInCategory(EventCategory category) const { return (GetCategoryFlags() & category) != EventCategory::None; }
	};

	class EventDispatcher
	{
	public:
		explicit EventDispatcher(Event& event) : m_Event(event) {}

		// Runs the handler only when the event is of the handler's type. Its answer is ORed
		// into Handled rather than assigned: an event consumed upstream stays consumed.
		template<std::derived_from<Event> T, std::predicate<T&> F>
		bool Dispatch(const F& handler)
		{
			// The language's own check rather than the EventType tag: nothing ties a tag to the
			// one class that returns it, and a cast on a mismatched tag would be undefined.
			T* const event = dynamic_cast<T*>(&m_Event);

			if (event == nullptr)
			{
				return false;
			}

			m_Event.Handled |= handler(*event);
			return true;
		}

	private:
		Event& m_Event;
	};
}
