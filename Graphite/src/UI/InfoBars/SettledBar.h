#pragma once

#include <optional>
#include <utility>

namespace Graphite {

	// Whether the pointer is at rest: no button held, and long enough since the last click that it cannot become a
	// double click. Only then may a bar above the editors appear or go, which moves everything below it.
	bool IsPointerSettled();

	// Holds an info bar as it was while the pointer is not settled, so a bar appearing or going never moves what is under
	// a click halfway through a double click. What a bar already shown says changes at once: that moves nothing.
	template<typename Bar>
	class SettledBar
	{
	public:
		const std::optional<Bar>& Update(std::optional<Bar> wanted, bool pointerSettled)
		{
			const bool movesNothing = wanted.has_value() == m_Shown.has_value();

			if (pointerSettled || movesNothing)
			{
				m_Shown = std::move(wanted);
			}

			return m_Shown;
		}

		const std::optional<Bar>& Shown() const { return m_Shown; }

	private:
		std::optional<Bar> m_Shown;
	};
}
