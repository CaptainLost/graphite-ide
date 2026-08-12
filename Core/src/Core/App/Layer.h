#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"

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
		virtual void OnUpdate(Timestep ts) { (void)ts; }
		virtual void OnUIRender() {}

		virtual bool OnCloseRequested() { return true; }

		const std::string& GetName() const { return m_Name; }

	private:
		std::string m_Name;
	};
}