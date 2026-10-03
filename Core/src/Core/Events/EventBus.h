#pragma once

#include "Core/Base/NonCopyable.h"

#include <concepts>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Core {

	template<typename E>
	concept EventPayload = std::is_class_v<E> && std::same_as<E, std::remove_cvref_t<E>>;

	class EventBus final : NonCopyable
	{
	public:
		// Keyed on typeid(E), so E is a plain class type: a const or reference spelling of one
		// event would otherwise key a different list from the one it is published to.
		template<EventPayload E>
		void Subscribe(std::function<void(const E&)> handler)
		{
			m_Handlers[std::type_index(typeid(E))].push_back(
				[handler = std::move(handler)](const void* payload) { handler(*static_cast<const E*>(payload)); });
		}

		template<EventPayload E>
		void Publish(const E& event)
		{
			const auto entry = m_Handlers.find(std::type_index(typeid(E)));
			if (entry == m_Handlers.end())
				return;

			// Copied because a handler may subscribe while this iterates, and a reallocated
			// vector would invalidate the iterator. Only subscription is guarded: a handler
			// that publishes the event type it is handling still recurses without bound.
			const std::vector<Handler> handlers = entry->second;

			for (const Handler& handler : handlers)
				handler(&event);
		}

		template<EventPayload E>
		std::size_t GetHandlerCount() const
		{
			const auto entry = m_Handlers.find(std::type_index(typeid(E)));
			return entry == m_Handlers.end() ? 0 : entry->second.size();
		}

	private:
		using Handler = std::function<void(const void*)>;

		std::unordered_map<std::type_index, std::vector<Handler>> m_Handlers;
	};
}