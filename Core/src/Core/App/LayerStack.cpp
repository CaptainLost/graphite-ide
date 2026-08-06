#include "corepch.h"

#include "Core/App/LayerStack.h"

#include "Core/Base/Assert.h"

namespace core
{
    LayerStack::~LayerStack()
    {
        Clear();
    }

    Layer& LayerStack::Push(std::unique_ptr<Layer> layer)
    {
        CORE_ASSERT(layer != nullptr, "LayerStack::Push dostal pusty wskaznik");

        Layer& reference = *layer;
        m_layers.push_back(std::move(layer));
        reference.OnAttach();
        return reference;
    }

    void LayerStack::Clear()
    {
        // Odwrotna kolejnosc: warstwa dodana pozniej moze zalezec od wczesniejszej.
        for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it)
            (*it)->OnDetach();

        m_layers.clear();
    }
}
