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

	class RowRenameField final
	{
	public:
		bool Start(std::string_view name, bool isFolder);

		RenameEnd Draw(const char* id);

		std::string Text() const { return m_Buffer.data(); }

	private:
		std::array<char, 1024> m_Buffer{};
		RenameSpan m_Span;
		bool m_Focus = false;
	};
}
