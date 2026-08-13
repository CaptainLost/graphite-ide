#include "Theme/ColorScheme.h"

namespace Graphite {

	std::string_view NameOf(TokenKind kind)
	{
		switch (kind)
		{
			case TokenKind::Text: return "text";
			case TokenKind::Keyword: return "keyword";
			case TokenKind::Identifier: return "identifier";
			case TokenKind::Number: return "number";
			case TokenKind::String: return "string";
			case TokenKind::Character: return "character";
			case TokenKind::Comment: return "comment";
			case TokenKind::Preprocessor: return "preprocessor";
			case TokenKind::Punctuation: return "punctuation";
			case TokenKind::Invalid: return "invalid";
			case TokenKind::Count: break;
		}

		return {};
	}

	std::string_view NameOf(EditorColor color)
	{
		switch (color)
		{
			case EditorColor::Background: return "background";
			case EditorColor::Text: return "text";
			case EditorColor::Selection: return "selection";
			case EditorColor::CurrentLine: return "currentLine";
			case EditorColor::Guide: return "guide";
			case EditorColor::GutterFill: return "gutterFill";
			case EditorColor::LineNumber: return "lineNumber";
			case EditorColor::Caret: return "caret";
			case EditorColor::Count: break;
		}

		return {};
	}

	ResolvedScheme Resolve(const ColorScheme& scheme, const ResolvedScheme& base)
	{
		ResolvedScheme resolved = base;

		for (std::size_t index = 0; index < scheme.Syntax.size(); ++index)
		{
			if (scheme.Syntax[index])
			{
				resolved.Syntax[index] = *scheme.Syntax[index];
			}
		}

		for (std::size_t index = 0; index < scheme.Editor.size(); ++index)
		{
			if (scheme.Editor[index])
			{
				resolved.Editor[index] = *scheme.Editor[index];
			}
		}

		return resolved;
	}
}
