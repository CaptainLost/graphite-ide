#pragma once

#include "Explorer/RenameSelection.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Graphite {

	enum class RenameEnd : std::uint8_t
	{
		StillEditing,
		Committed,
		Cancelled
	};

	// The field a tree row turns into while its name is edited, in every tree alike: at the row's label, as wide as the
	// name, with the name but its extension selected when it opens. Enter or a click elsewhere commits it, Esc cancels.
	class RowRenameField final
	{
	public:
		// False, and nothing started, for a name too long for the field.
		bool Start(std::string_view name, bool isFolder);

		// At the cursor, where the row would be drawn.
		RenameEnd Draw(const char* id);

		std::string Text() const { return m_Buffer.data(); }

	private:
		std::array<char, 1024> m_Buffer{};
		RenameSpan m_Span;
		bool m_Focus = false;
	};
}
