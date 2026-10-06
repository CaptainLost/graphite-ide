#pragma once

#include "Core/Base/Timestep.h"

#include <chrono>
#include <optional>

namespace Core {

	class FramePacer
	{
	public:
		using Clock = SteadyClock;

		static constexpr Clock::duration kInputWindow = std::chrono::seconds(1);
		static constexpr Clock::duration kIdleTick = std::chrono::seconds(1);

		static constexpr int kFramesToDrainResult = 1;
		// ImGui can need up to two frames to settle some layout after the change behind it: a
		// window fitting new content, a table sizing a new column.
		static constexpr int kFramesImGuiSettlesLate = 2;
		static constexpr int kRequestFrames = kFramesToDrainResult + kFramesImGuiSettlesLate;

		void NoteInput(Clock::time_point now);
		void NoteRequest();
		void FrameDrawn();

		Clock::duration SleepFor(Clock::time_point now, bool pointerHeld,
								 std::optional<Clock::time_point> deadline) const;

	private:
		Clock::time_point m_InputWindowEnd{};
		int m_RequestFramesLeft = 0;
	};
}
