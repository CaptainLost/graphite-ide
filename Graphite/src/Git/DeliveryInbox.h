#pragma once

#include <functional>
#include <mutex>
#include <vector>

namespace Graphite {

	// WARNING: The mutex covers the queue and nothing else. Whatever a delivery touches is only
	// ever touched on the UI thread, from Drain.
	class DeliveryInbox
	{
	public:
		using Delivery = std::function<void()>;

		// Callable from any thread.
		void Post(Delivery delivery);

		// UI thread only. WARNING: A delivery posted while draining waits for the next Drain.
		void Drain();

	private:
		std::mutex m_Mutex;
		std::vector<Delivery> m_Queue;
	};
}
