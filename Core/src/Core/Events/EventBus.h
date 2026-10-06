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

	template<typename Payload>
	concept EventPayload = std::is_class_v<Payload> && std::same_as<Payload, std::remove_cvref_t<Payload>>;

	class EventBus final : NonCopyable
	{
	public:
		template<EventPayload Payload>
		void Subscribe(std::function<void(const Payload&)> handler)
		{
			m_Handlers[std::type_index(typeid(Payload))].push_back(
				[handler = std::move(handler)](const void* erasedPayload) { handler(*static_cast<const Payload*>(erasedPayload)); });
		}

		template<EventPayload Payload>
		void Publish(const Payload& event)
		{
			const std::type_index payloadType{typeid(Payload)};

			if (!m_Handlers.contains(payloadType))
			{
				return;
			}

			// A copy, because a handler may subscribe while this iterates, and a reallocated vector
			// would invalidate the iterator. Only subscription is guarded: a handler that publishes
			// the event type it is handling still recurses without bound.
			const std::vector<Handler> handlersAtPublish = m_Handlers.at(payloadType);

			for (const Handler& handler : handlersAtPublish)
			{
				handler(&event);
			}
		}

		template<EventPayload Payload>
		std::size_t GetHandlerCount() const
		{
			const std::type_index payloadType{typeid(Payload)};

			if (!m_Handlers.contains(payloadType))
			{
				return 0;
			}

			return m_Handlers.at(payloadType).size();
		}

	private:
		using Handler = std::function<void(const void*)>;

		std::unordered_map<std::type_index, std::vector<Handler>> m_Handlers;
	};
}