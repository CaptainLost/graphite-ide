#pragma once

#include "Core/Base/Base.h"

#include <concepts>
#include <functional>
#include <string>

namespace Core {

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

	// Dispatched the moment it arrives, not queued: a handler that consumes one has to do so
	// before the frame that produced it draws, or the shortcut and the text field both see the
	// same key.
	class Event
	{
	public:
		virtual ~Event() = default;

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

		template<std::derived_from<Event> EventClass>
		bool Dispatch(const std::function<bool(EventClass&)>& handler)
		{
			// The language's own check rather than the EventType tag: nothing ties a tag to the
			// one class that returns it, and a cast on a mismatched tag would be undefined.
			EventClass* const matchingEvent = dynamic_cast<EventClass*>(&m_Event);

			if (matchingEvent == nullptr)
			{
				return false;
			}

			const bool handlerConsumed = handler(*matchingEvent);
			m_Event.Handled = m_Event.Handled || handlerConsumed;
			return true;
		}

	private:
		Event& m_Event;
	};
}
