#include "corepch.h"

#include "Core/App/LayerStack.h"

#include "Core/Base/Base.h"

#include "Core/Base/Assert.h"

namespace Core {

	LayerStack::~LayerStack()
	{
		Clear();
	}

	Layer& LayerStack::Push(Scope<Layer> layer)
	{
		CORE_ASSERT(layer != nullptr, "LayerStack::Push received a null pointer");

		Layer& reference = *layer;
		m_Layers.push_back(std::move(layer));
		reference.OnAttach();
		return reference;
	}

	void LayerStack::Clear()
	{
		// Reverse order: a layer added later may depend on an earlier one.
		for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
		{
			(*it)->OnDetach();
		}

		m_Layers.clear();
	}
}