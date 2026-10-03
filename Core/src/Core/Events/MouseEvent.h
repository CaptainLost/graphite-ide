#pragma once

#include "Core/Base/MouseCodes.h"
#include "Core/Events/Event.h"

#include <format>
#include <string>

namespace Core {

	class MouseMovedEvent final : public Event
	{
	public:
		MouseMovedEvent(float x, float y) : m_X(x), m_Y(y) {}

		float GetX() const { return m_X; }
		float GetY() const { return m_Y; }

		std::string ToString() const override { return std::format("MouseMovedEvent: {}, {}", m_X, m_Y); }

		CORE_EVENT_CLASS_TYPE(MouseMoved)
		CORE_EVENT_CLASS_CATEGORY(EventCategory::Mouse | EventCategory::Input)

	private:
		float m_X = 0.0f;
		float m_Y = 0.0f;
	};

	class MouseScrolledEvent final : public Event
	{
	public:
		MouseScrolledEvent(float xOffset, float yOffset) : m_XOffset(xOffset), m_YOffset(yOffset) {}

		float GetXOffset() const { return m_XOffset; }
		float GetYOffset() const { return m_YOffset; }

		std::string ToString() const override
		{
			return std::format("MouseScrolledEvent: {}, {}", m_XOffset, m_YOffset);
		}

		CORE_EVENT_CLASS_TYPE(MouseScrolled)
		CORE_EVENT_CLASS_CATEGORY(EventCategory::Mouse | EventCategory::Input)

	private:
		float m_XOffset = 0.0f;
		float m_YOffset = 0.0f;
	};

	class MouseButtonEvent : public Event
	{
	public:
		MouseCode GetMouseButton() const { return m_Button; }

		CORE_EVENT_CLASS_CATEGORY(EventCategory::Mouse | EventCategory::MouseButton | EventCategory::Input)

	protected:
		explicit MouseButtonEvent(MouseCode button) : m_Button(button) {}

	private:
		MouseCode m_Button = MouseCode::Left;
	};

	class MouseButtonPressedEvent final : public MouseButtonEvent
	{
	public:
		explicit MouseButtonPressedEvent(MouseCode button) : MouseButtonEvent(button) {}

		std::string ToString() const override
		{
			return std::format("MouseButtonPressedEvent: {}", static_cast<int>(GetMouseButton()));
		}

		CORE_EVENT_CLASS_TYPE(MouseButtonPressed)
	};

	class MouseButtonReleasedEvent final : public MouseButtonEvent
	{
	public:
		explicit MouseButtonReleasedEvent(MouseCode button) : MouseButtonEvent(button) {}

		std::string ToString() const override
		{
			return std::format("MouseButtonReleasedEvent: {}", static_cast<int>(GetMouseButton()));
		}

		CORE_EVENT_CLASS_TYPE(MouseButtonReleased)
	};
}
