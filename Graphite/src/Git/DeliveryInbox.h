#pragma once

#include <functional>
#include <mutex>
#include <vector>

namespace Graphite {

	// Where a worker leaves a finished answer for the UI thread to apply.
	//
	// The mutex covers the queue and nothing else: whatever a delivery touches is only ever
	// touched on the UI thread, from Drain.
	class DeliveryInbox
	{
	public:
		using Delivery = std::function<void()>;

		// Any thread. Wakes the frame loop so the delivery is applied without waiting for its
		// idle tick.
		void Post(Delivery delivery);

		// UI thread. Runs everything posted so far, in order. A delivery posted while draining
		// waits for the next Drain.
		void Drain();

	private:
		std::mutex m_Mutex;
		std::vector<Delivery> m_Queue;
	};
}
