#pragma once

#include "Core/Base/NonCopyable.h"

#include <cstddef>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Core
{
    /// Typed publish/subscribe, so panels can react to each other without knowing
    /// each other. Handlers cannot be removed: subscribers live exactly as long as
    /// the layer that owns the bus, and PanelManager has no removal operation. An
    /// RAII subscription handle arrives together with removal, if it ever does.
    class EventBus final : NonCopyable
    {
    public:
        template<typename E>
        void Subscribe(std::function<void(const E&)> handler)
        {
            m_Handlers[std::type_index(typeid(E))].push_back(
                [handler = std::move(handler)](const void* payload)
                { handler(*static_cast<const E*>(payload)); });
        }

        template<typename E>
        void Publish(const E& event)
        {
            const auto entry = m_Handlers.find(std::type_index(typeid(E)));
            if (entry == m_Handlers.end())
                return;

            // Dispatch over a copy: a handler may subscribe while we are iterating,
            // and a reallocated vector would invalidate the iterator underneath us.
            // This only guards against subscribing during dispatch — a handler that
            // publishes the same event type it is handling recurses without bound,
            // the copy does nothing to stop that.
            const std::vector<Handler> handlers = entry->second;

            for (const Handler& handler : handlers)
                handler(&event);
        }

        template<typename E>
        [[nodiscard]] std::size_t GetHandlerCount() const
        {
            const auto entry = m_Handlers.find(std::type_index(typeid(E)));
            return entry == m_Handlers.end() ? 0 : entry->second.size();
        }

    private:
        using Handler = std::function<void(const void*)>;

        std::unordered_map<std::type_index, std::vector<Handler>> m_Handlers;
    };
}
