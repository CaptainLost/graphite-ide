#pragma once

#include "Core/Base/Base.h"

#include "Core/App/Layer.h"
#include "Core/Base/NonCopyable.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace Core {

	class LayerStack final : NonCopyable
	{
	public:
		using Container = std::vector<Scope<Layer>>;

		LayerStack() = default;
		~LayerStack();

		Layer& Push(Scope<Layer> layer);
		void Clear();

		void OfferEventFromTheTopUntilHandled(Event& event);

		std::size_t GetSize() const { return m_Layers.size(); }

		Container::iterator begin() { return m_Layers.begin(); }
		Container::iterator end() { return m_Layers.end(); }
		Container::const_iterator begin() const { return m_Layers.begin(); }
		Container::const_iterator end() const { return m_Layers.end(); }

		Container::reverse_iterator rbegin() { return m_Layers.rbegin(); }
		Container::reverse_iterator rend() { return m_Layers.rend(); }

	private:
		Container m_Layers;
	};
}