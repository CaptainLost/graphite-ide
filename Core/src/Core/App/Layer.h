#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/Event.h"

#include <string>
#include <utility>

namespace Core {

	class Layer : NonCopyable
	{
	public:
		explicit Layer(std::string name) : m_Name(std::move(name)) {}
		virtual ~Layer() = default;

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate(Timestep) {}
		virtual void OnImGuiRender() {}
		virtual void OnEvent(Event&) {}

		// Returning true later does not close the window by itself: refusing already cleared the
		// flag the loop tests, so a layer that has finished asking must call
		// Window::RequestClose() itself.
		virtual bool OnCloseRequested() { return true; }

		const std::string& GetName() const { return m_Name; }

	private:
		std::string m_Name;
	};
}