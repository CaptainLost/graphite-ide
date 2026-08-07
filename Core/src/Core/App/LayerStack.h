#pragma once

#include "Core/App/Layer.h"
#include "Core/Base/NonCopyable.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace core
{
    class LayerStack final : NonCopyable
    {
    public:
        using Container = std::vector<std::unique_ptr<Layer>>;

        LayerStack() = default;
        ~LayerStack();

        Layer& Push(std::unique_ptr<Layer> layer);
        void Clear();

        [[nodiscard]] std::size_t Size() const noexcept { return m_layers.size(); }

        Container::iterator begin() noexcept { return m_layers.begin(); }
        Container::iterator end() noexcept { return m_layers.end(); }
        Container::const_iterator begin() const noexcept { return m_layers.begin(); }
        Container::const_iterator end() const noexcept { return m_layers.end(); }

    private:
        Container m_layers;
    };
}
