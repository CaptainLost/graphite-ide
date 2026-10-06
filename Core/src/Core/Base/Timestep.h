#pragma once

#include <chrono>

namespace Core {

	using SteadyClock = std::chrono::steady_clock;

	// No implicit conversion either way: a bare float does not say it is seconds.
	class Timestep
	{
	public:
		constexpr Timestep() = default;
		constexpr explicit Timestep(float seconds) : m_Seconds(seconds) {}

		float GetSeconds() const { return m_Seconds; }
		float GetMilliseconds() const { return m_Seconds * kMillisecondsPerSecond; }

	private:
		static constexpr float kMillisecondsPerSecond = 1000.0f;

		float m_Seconds = 0.0f;
	};
}