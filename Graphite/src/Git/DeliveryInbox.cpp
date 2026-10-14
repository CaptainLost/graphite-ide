#include "Git/DeliveryInbox.h"

#include "Core/App/Redraw.h"

#include <utility>

namespace Graphite {

	void DeliveryInbox::Post(Delivery delivery)
	{
		{
			const std::lock_guard<std::mutex> queueLock{m_Mutex};
			m_Queue.push_back(std::move(delivery));
		}

		Core::Redraw::Request();
	}

	void DeliveryInbox::Drain()
	{
		std::vector<Delivery> readyDeliveries;

		{
			const std::lock_guard<std::mutex> queueLock{m_Mutex};
			readyDeliveries.swap(m_Queue);
		}

		for (const Delivery& delivery : readyDeliveries)
		{
			delivery();
		}
	}
}
