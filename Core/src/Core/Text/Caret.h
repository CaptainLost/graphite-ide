#pragma once

#include <cstddef>
#include <limits>

namespace Core {

	class TextBuffer;

	inline constexpr std::size_t NoDesiredColumn = std::numeric_limits<std::size_t>::max();

	struct Caret
	{
		std::size_t Offset = 0;

		std::size_t Anchor = 0;

		std::size_t DesiredColumn = NoDesiredColumn;

		friend bool operator==(const Caret&, const Caret&) = default;
	};

	enum class Motion
	{
		Left,
		Right,
		Up,
		Down,
		LineStart,
		LineEnd,
		DocumentStart,
		DocumentEnd,
		WordLeft,
		WordRight,
		PageUp,
		PageDown
	};

	enum class SelectionChange
	{
		Collapse,
		Extend
	};

	struct OffsetRange
	{
		std::size_t From = 0;
		std::size_t To = 0;

		friend bool operator==(const OffsetRange&, const OffsetRange&) = default;
	};

	// What a drag that started on a double or triple click grows the selection by.
	enum class SelectionUnit
	{
		Character,
		Word,
		Line
	};

	bool HasSelection(const Caret& caret);

	OffsetRange SelectionRange(const Caret& caret);

	Caret Move(const TextBuffer& buffer, Caret caret, Motion motion, SelectionChange selectionChange,
			   std::size_t tabWidth, std::size_t pageLines);

	Caret PlaceAt(const TextBuffer& buffer, Caret caret, std::size_t offset,
				  SelectionChange selectionChange, std::size_t tabWidth);

	std::size_t CharacterBoundaryAtOrBefore(const TextBuffer& buffer, std::size_t offset);

	Caret SelectWordAt(const TextBuffer& buffer, std::size_t offset);

	Caret SelectLineAt(const TextBuffer& buffer, std::size_t line);

	// The selection while a drag that began on `origin` (the word or line first selected) reaches
	// `offset`: whole words or lines, always containing the origin, whichever side the pointer is on.
	Caret ExtendByUnit(const TextBuffer& buffer, OffsetRange origin, std::size_t offset,
					   SelectionUnit unit);

	Caret SelectAll(const TextBuffer& buffer);
}