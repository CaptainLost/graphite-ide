#include "Editor/SyntaxPalette.h"

namespace Graphite {

	namespace {

		void Set(SyntaxPalette& palette, TokenKind kind, ImU32 colour)
		{
			palette.Colours[static_cast<std::size_t>(kind)] = colour;
		}
	}

	SyntaxPalette ReadSyntaxPalette(ImU32 text)
	{
		SyntaxPalette palette;

		// Every entry starts as the text colour, including any kind this function never
		// mentions below — a kind added to TokenKind and not yet given a colour here draws as
		// plain text rather than reading whatever was left in an uninitialised array slot.
		palette.Colours.fill(text);

		Set(palette, TokenKind::Keyword, IM_COL32(197, 134, 192, 255));
		Set(palette, TokenKind::Number, IM_COL32(181, 206, 168, 255));
		Set(palette, TokenKind::String, IM_COL32(206, 145, 120, 255));
		Set(palette, TokenKind::Character, IM_COL32(206, 145, 120, 255));
		Set(palette, TokenKind::Comment, IM_COL32(106, 153, 85, 255));
		Set(palette, TokenKind::Preprocessor, IM_COL32(155, 155, 155, 255));
		// Not red: an unterminated literal is what every half-typed line looks like, and stage 7
		// owns the colour that means "error". Left at the text colour Colours.fill already gave
		// it, along with Text, Identifier and Punctuation.

		return palette;
	}
}
