#pragma once

namespace Core
{
    /// The time one frame took. Converts to float implicitly so arithmetic reads as it
    /// would with a bare delta, while the type still says which unit that float is in —
    /// seconds. Ask GetMilliseconds() rather than multiplying at the call site.
    class Timestep
    {
    public:
        Timestep(float time = 0.0f) : m_Time(time) {}

        operator float() const noexcept { return m_Time; }

        [[nodiscard]] float GetSeconds() const noexcept { return m_Time; }
        [[nodiscard]] float GetMilliseconds() const noexcept { return m_Time * 1000.0f; }

    private:
        float m_Time;
    };
}
