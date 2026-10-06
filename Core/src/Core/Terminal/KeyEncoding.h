#pragma once

#include "Core/Base/KeyCodes.h"

#include <cstdint>
#include <string>

namespace Core {

	// Empty for a modifier held on its own and for a printable character. Printable characters
	// reach the terminal through the host's character input, which has already applied the
	// keyboard layout, so encoding them here as well would send each one twice.
	std::string EncodeKey(KeyCode key, KeyModifier modifiers);

	enum class WheelDirection : std::uint8_t
	{
		Up,
		Down
	};

	enum class MouseReportForm : std::uint8_t
	{
		Legacy,
		SgrDecimal
	};

	std::string EncodeMouseWheel(WheelDirection direction, std::uint16_t zeroBasedColumn, std::uint16_t zeroBasedRow,
								 KeyModifier modifiers, MouseReportForm form);
}
