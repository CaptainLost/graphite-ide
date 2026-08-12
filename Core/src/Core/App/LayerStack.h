#pragma once

#include "Core/App/Layer.h"
#include "Core/Base/NonCopyable.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace Core {

	class LayerStack final : NonCopyable
	{
	public:
		using Container = std::vector<std::unique_ptr<Layer>>;

		LayerStack() = default;
		~LayerStack();

		Layer& Push(std::unique_ptr<Layer> layer);
		void Clear();

		std::size_t GetSize() const { return m_Layers.size(); }

		Container::iterator begin() { return m_Layers.begin(); }
		Container::iterator end() { return m_Layers.end(); }
		Container::const_iterator begin() const { return m_Layers.begin(); }
		Container::const_iterator end() const { return m_Layers.end(); }

	private:
		Container m_Layers;
	};
}