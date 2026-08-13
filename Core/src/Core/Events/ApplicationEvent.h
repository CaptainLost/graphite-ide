#pragma once

#include "Core/Events/Event.h"

#include <format>

namespace Core {

	class WindowResizeEvent final : public Event
	{
	public:
		WindowResizeEvent(int width, int height) : m_Width(width), m_Height(height) {}

		int GetWidth() const { return m_Width; }
		int GetHeight() const { return m_Height; }

		std::string ToString() const override
		{
			return std::format("WindowResizeEvent: {}, {}", m_Width, m_Height);
		}

		EVENT_CLASS_TYPE(WindowResize)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		int m_Width = 0;
		int m_Height = 0;
	};

	class WindowCloseEvent final : public Event
	{
	public:
		EVENT_CLASS_TYPE(WindowClose)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowFocusEvent final : public Event
	{
	public:
		EVENT_CLASS_TYPE(WindowFocus)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowLostFocusEvent final : public Event
	{
	public:
		EVENT_CLASS_TYPE(WindowLostFocus)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowMovedEvent final : public Event
	{
	public:
		WindowMovedEvent(int x, int y) : m_X(x), m_Y(y) {}

		int GetX() const { return m_X; }
		int GetY() const { return m_Y; }

		std::string ToString() const override { return std::format("WindowMovedEvent: {}, {}", m_X, m_Y); }

		EVENT_CLASS_TYPE(WindowMoved)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		int m_X = 0;
		int m_Y = 0;
	};
}
