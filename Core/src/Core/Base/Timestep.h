#pragma once

namespace Core {

	class Timestep
	{
	public:
		// Explicit both ways: a bare float is not obviously seconds, and a Timestep read as a
		// bare float loses the unit it carries. GetSeconds and GetMilliseconds say which.
		constexpr Timestep() = default;
		constexpr explicit Timestep(float seconds) : m_Time(seconds) {}

		float GetSeconds() const { return m_Time; }
		float GetMilliseconds() const { return m_Time * 1000.0f; }

	private:
		float m_Time = 0.0f;
	};
}