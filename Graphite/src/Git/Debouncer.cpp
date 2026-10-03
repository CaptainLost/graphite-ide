#include "Git/Debouncer.h"

#include "Core/App/Redraw.h"

#include <algorithm>
#include <chrono>

namespace Graphite {

	void Debouncer::Mark()
	{
		if (!m_Dirty)
		{
			m_DirtySince = 0.0f;
		}

		m_Dirty = true;
		m_DirtyFor = 0.0f;

		RequestFrame();
	}

	bool Debouncer::Advance(Core::Timestep step)
	{
		const float seconds = step.GetSeconds();

		bool fired = false;

		if (m_Dirty)
		{
			m_DirtyFor += seconds;
			m_DirtySince += seconds;

			if (m_DirtyFor >= kQuietSeconds || m_DirtySince >= kMaxSeconds)
			{
				Reset();
				fired = true;
			}
		}

		RequestFrame();
		return fired;
	}

	void Debouncer::Reset()
	{
		m_Dirty = false;
		m_DirtyFor = 0.0f;
		m_DirtySince = 0.0f;
	}

	void Debouncer::RequestFrame() const
	{
		if (!m_Dirty)
		{
			return;
		}

		const float remaining = std::min(kQuietSeconds - m_DirtyFor, kMaxSeconds - m_DirtySince);

		Core::Redraw::RequestIn(std::chrono::duration<double>(std::max(remaining, 0.0f)));
	}
}
