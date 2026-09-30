#include "corepch.h"

#include "Core/App/FramePacer.h"

#include <algorithm>

namespace Core {

	void FramePacer::NoteInput(Clock::time_point now)
	{
		m_InputUntil = std::max(m_InputUntil, now + InputWindow);
	}

	void FramePacer::NoteRequest()
	{
		m_FramesOwed = std::max(m_FramesOwed, RequestFrames);
	}

	void FramePacer::FrameDrawn()
	{
		if (m_FramesOwed > 0)
			--m_FramesOwed;
	}

	FramePacer::Clock::duration FramePacer::SleepFor(Clock::time_point now, bool pointerHeld,
													 std::optional<Clock::time_point> deadline) const
	{
		if (pointerHeld || m_FramesOwed > 0 || now < m_InputUntil)
			return Clock::duration::zero();

		Clock::time_point wake = now + IdleTick;

		if (deadline)
			wake = std::clamp(*deadline, now, wake);

		return wake - now;
	}
}
