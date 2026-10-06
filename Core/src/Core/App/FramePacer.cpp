#include "corepch.h"

#include "Core/App/FramePacer.h"

#include <algorithm>

namespace Core {

	void FramePacer::NoteInput(Clock::time_point now)
	{
		m_InputWindowEnd = std::max(m_InputWindowEnd, now + kInputWindow);
	}

	void FramePacer::NoteRequest()
	{
		m_RequestFramesLeft = std::max(m_RequestFramesLeft, kRequestFrames);
	}

	void FramePacer::FrameDrawn()
	{
		if (m_RequestFramesLeft > 0)
		{
			--m_RequestFramesLeft;
		}
	}

	FramePacer::Clock::duration FramePacer::SleepFor(Clock::time_point now, bool pointerHeld,
													 std::optional<Clock::time_point> deadline) const
	{
		const bool requestFramesLeft = m_RequestFramesLeft > 0;
		const bool insideInputWindow = now < m_InputWindowEnd;
		const bool drawStraightAway = pointerHeld || requestFramesLeft || insideInputWindow;

		if (drawStraightAway)
		{
			return Clock::duration::zero();
		}

		Clock::time_point wakeTime = now + kIdleTick;

		if (deadline.has_value())
		{
			wakeTime = std::clamp(*deadline, now, wakeTime);
		}

		return wakeTime - now;
	}
}
