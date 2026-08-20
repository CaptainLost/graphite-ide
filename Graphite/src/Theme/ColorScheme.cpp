#include "Theme/ColorScheme.h"

namespace Graphite {

	ColorNames NamesOf(TokenKind kind)
	{
		switch (kind)
		{
			case TokenKind::Text: return {"text", "Text"};
			case TokenKind::Keyword: return {"keyword", "Keyword"};
			case TokenKind::Identifier: return {"identifier", "Identifier"};
			case TokenKind::Number: return {"number", "Number"};
			case TokenKind::String: return {"string", "String"};
			case TokenKind::Character: return {"character", "Character"};
			case TokenKind::Comment: return {"comment", "Comment"};
			case TokenKind::Preprocessor: return {"preprocessor", "Preprocessor"};
			case TokenKind::Punctuation: return {"punctuation", "Punctuation"};
			case TokenKind::Invalid: return {"invalid", "Invalid"};
			case TokenKind::SemanticType: return {"semanticType", "Type (semantic)"};
			case TokenKind::SemanticFunction: return {"semanticFunction", "Function (semantic)"};
			case TokenKind::SemanticMacro: return {"semanticMacro", "Macro (semantic)"};
			case TokenKind::SemanticMember: return {"semanticMember", "Member (semantic)"};
			case TokenKind::SemanticParameter: return {"semanticParameter", "Parameter (semantic)"};
			case TokenKind::SemanticNamespace: return {"semanticNamespace", "Namespace (semantic)"};
			case TokenKind::Count: break;
		}

		return {};
	}

	ColorNames NamesOf(EditorColor color)
	{
		switch (color)
		{
			case EditorColor::Background: return {"background", "Background"};
			case EditorColor::Text: return {"text", "Text"};
			case EditorColor::Selection: return {"selection", "Selection"};
			case EditorColor::CurrentLine: return {"currentLine", "Current line"};
			case EditorColor::Guide: return {"guide", "Indent guide"};
			case EditorColor::GutterFill: return {"gutterFill", "Gutter"};
			case EditorColor::LineNumber: return {"lineNumber", "Line number"};
			case EditorColor::Caret: return {"caret", "Caret"};
			case EditorColor::FindMatch: return {"findMatch", "Find match"};
			case EditorColor::FindCurrentMatch: return {"findCurrentMatch", "Current find match"};
			case EditorColor::Lens: return {"lens", "Reference count"};
			case EditorColor::Count: break;
		}

		return {};
	}

	ColorNames NamesOf(ChromeColor color)
	{
		switch (color)
		{
			case ChromeColor::Background: return {"background", "Background"};
			case ChromeColor::Surface: return {"surface", "Surface"};
			case ChromeColor::SurfaceHovered: return {"surfaceHovered", "Surface (hovered)"};
			case ChromeColor::SurfaceActive: return {"surfaceActive", "Surface (active)"};
			case ChromeColor::Accent: return {"accent", "Accent"};
			case ChromeColor::Text: return {"text", "Text"};
			case ChromeColor::TextDisabled: return {"textDisabled", "Text (disabled)"};
			case ChromeColor::Border: return {"border", "Border"};
			case ChromeColor::Count: break;
		}

		return {};
	}

	std::string_view NameOf(TokenKind kind) { return NamesOf(kind).Key; }

	std::string_view NameOf(EditorColor color) { return NamesOf(color).Key; }

	std::string_view NameOf(ChromeColor color) { return NamesOf(color).Key; }

	std::string_view LabelOf(TokenKind kind) { return NamesOf(kind).Label; }

	std::string_view LabelOf(EditorColor color) { return NamesOf(color).Label; }

	std::string_view LabelOf(ChromeColor color) { return NamesOf(color).Label; }

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

		for (std::size_t index = 0; index < scheme.Chrome.size(); ++index)
		{
			if (scheme.Chrome[index])
			{
				resolved.Chrome[index] = *scheme.Chrome[index];
			}
		}

		return resolved;
	}
}
