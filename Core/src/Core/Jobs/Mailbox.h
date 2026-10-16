#pragma once

#include "Core/App/Redraw.h"

#include <mutex>
#include <utility>
#include <vector>

namespace Core {

	// What a worker hands the UI thread. WARNING: A worker that can outlive the mailbox's owner must hold it through a
	// shared pointer, so a job still finishing as the owner goes away has somewhere to post.
	template<typename T>
	class Mailbox
	{
	public:
		// Any thread. Asks for a frame, so the UI thread takes the item without waiting for its idle tick.
		void Post(T item)
		{
			{
				const std::lock_guard<std::mutex> lock(m_Mutex);
				m_Items.push_back(std::move(item));
			}

			Redraw::Request();
		}

		// In the order posted; the mailbox is empty afterwards.
		std::vector<T> TakeAll()
		{
			std::vector<T> taken;

			const std::lock_guard<std::mutex> lock(m_Mutex);
			taken.swap(m_Items);

			return taken;
		}

		void Clear()
		{
			const std::lock_guard<std::mutex> lock(m_Mutex);
			m_Items.clear();
		}

	private:
		std::mutex m_Mutex;
		std::vector<T> m_Items;
	};
}