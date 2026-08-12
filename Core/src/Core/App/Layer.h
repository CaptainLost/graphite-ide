#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"

#include <string>
#include <utility>

namespace Core {

	class Layer : NonCopyable
	{
	public:
		explicit Layer(std::string name) : m_DebugName(std::move(name)) {}
		virtual ~Layer() = default;

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate(Timestep ts) { (void)ts; }
		virtual void OnImGuiRender() {}

		// Returning true does not by itself close the window: refusing already cleared the
		// flag the loop tests, so a layer that has finished asking must call
		// Window::RequestClose() itself. A layer that refuses is asked again every frame,
		// which is what lets a modal draw at all.
		virtual bool OnCloseRequested() { return true; }

		const std::string& GetName() const { return m_DebugName; }

	protected:
		std::string m_DebugName;
	};
}