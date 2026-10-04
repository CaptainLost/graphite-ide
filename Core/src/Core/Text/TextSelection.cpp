#include "corepch.h"

#include "Core/Text/TextSelection.h"

#include <algorithm>

namespace Core {

	namespace {
		// Moves `byte` back to the first byte of the character it falls inside.
		std::size_t CharacterStart(std::string_view text, std::size_t byte)
		{
			byte = std::min(byte, text.size());
			while (byte > 0 && byte < text.size() &&
				   (static_cast<unsigned char>(text[byte]) & 0xC0) == 0x80)
			{
				--byte;
			}
			return byte;
		}
	}

	void TextSelection::Begin(TextPoint at)
	{
		m_Pressed = at;
		m_Dragged = at;
	}

	void TextSelection::ExtendTo(TextPoint at)
	{
		m_Dragged = at;
	}

	void TextSelection::Select(TextPoint from, TextPoint to)
	{
		m_Pressed = from;
		m_Dragged = to;
	}

	void TextSelection::Clear()
	{
		m_Pressed = TextPoint{};
		m_Dragged = TextPoint{};
	}

	bool TextSelection::IsEmpty() const
	{
		return m_Pressed == m_Dragged;
	}

	TextPoint TextSelection::First() const
	{
		return std::min(m_Pressed, m_Dragged);
	}

	TextPoint TextSelection::Last() const
	{
		return std::max(m_Pressed, m_Dragged);
	}

	std::optional<ColumnSpan> TextSelection::SpanOn(std::int64_t line, std::size_t length) const
	{
		if (IsEmpty())
		{
			return std::nullopt;
		}

		const TextPoint first = First();
		const TextPoint last = Last();

		if (line < first.Line || line > last.Line)
		{
			return std::nullopt;
		}

		const std::size_t from = line == first.Line ? std::min(first.Column, length) : 0;
		const std::size_t to = line == last.Line ? std::min(last.Column, length) : length;

		return ColumnSpan{from, std::max(from, to)};
	}

	std::string JoinSelected(const TextSelection& selection, std::span<const SelectableLine> lines)
	{
		std::string out;
		bool wrote = false;

		for (const SelectableLine& line : lines)
		{
			const std::optional<ColumnSpan> span = selection.SpanOn(line.Line, line.Text.size());

			if (!span.has_value())
			{
				continue;
			}

			if (wrote)
			{
				out.push_back('\n');
			}

			wrote = true;
			const std::size_t from = CharacterStart(line.Text, span->From);
			const std::size_t to = CharacterStart(line.Text, span->To);
			out.append(line.Text.substr(from, to - from));
		}

		return out;
	}
}
