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

		Layer& pushedLayer = *layer;
		m_Layers.push_back(std::move(layer));
		pushedLayer.OnAttach();
		return pushedLayer;
	}

	void LayerStack::Clear()
	{
		for (Container::reverse_iterator layerFromTheTop = m_Layers.rbegin(); layerFromTheTop != m_Layers.rend(); ++layerFromTheTop)
		{
			(*layerFromTheTop)->OnDetach();
		}

		m_Layers.clear();
	}

	void LayerStack::OfferEventFromTheTopUntilHandled(Event& event)
	{
		for (Container::reverse_iterator layerFromTheTop = m_Layers.rbegin(); layerFromTheTop != m_Layers.rend(); ++layerFromTheTop)
		{
			if (event.Handled)
			{
				break;
			}

			(*layerFromTheTop)->OnEvent(event);
		}
	}
}