#include "Theme/ColorScheme.h"

namespace Graphite {

	ColorNames NamesOf(TokenKind kind)
	{
		switch (kind)
		{
		case TokenKind::Text:
			return {"text", "Text"};
		case TokenKind::Keyword:
			return {"keyword", "Keyword"};
		case TokenKind::Identifier:
			return {"identifier", "Identifier"};
		case TokenKind::Number:
			return {"number", "Number"};
		case TokenKind::String:
			return {"string", "String"};
		case TokenKind::Character:
			return {"character", "Character"};
		case TokenKind::Comment:
			return {"comment", "Comment"};
		case TokenKind::Preprocessor:
			return {"preprocessor", "Preprocessor"};
		case TokenKind::Punctuation:
			return {"punctuation", "Punctuation"};
		case TokenKind::Invalid:
			return {"invalid", "Invalid"};
		case TokenKind::SemanticType:
			return {"semanticType", "Type (semantic)"};
		case TokenKind::SemanticFunction:
			return {"semanticFunction", "Function (semantic)"};
		case TokenKind::SemanticMacro:
			return {"semanticMacro", "Macro (semantic)"};
		case TokenKind::SemanticMember:
			return {"semanticMember", "Member (semantic)"};
		case TokenKind::SemanticParameter:
			return {"semanticParameter", "Parameter (semantic)"};
		case TokenKind::SemanticNamespace:
			return {"semanticNamespace", "Namespace (semantic)"};
		case TokenKind::Count:
			break;
		}

		return {};
	}

	ColorNames NamesOf(EditorColor color)
	{
		switch (color)
		{
		case EditorColor::Background:
			return {"background", "Background"};
		case EditorColor::Text:
			return {"text", "Text"};
		case EditorColor::Selection:
			return {"selection", "Selection"};
		case EditorColor::SelectionOccurrence:
			return {"selectionOccurrence", "Selection occurrence"};
		case EditorColor::CurrentLine:
			return {"currentLine", "Current line"};
		case EditorColor::Guide:
			return {"guide", "Indent guide"};
		case EditorColor::GutterFill:
			return {"gutterFill", "Gutter"};
		case EditorColor::LineNumber:
			return {"lineNumber", "Line number"};
		case EditorColor::CurrentLineNumber:
			return {"currentLineNumber", "Current line number"};
		case EditorColor::Caret:
			return {"caret", "Caret"};
		case EditorColor::FindMatch:
			return {"findMatch", "Find match"};
		case EditorColor::FindCurrentMatch:
			return {"findCurrentMatch", "Current find match"};
		case EditorColor::Lens:
			return {"lens", "Reference count"};
		case EditorColor::LineAuthor:
			return {"lineAuthor", "Line author"};
		case EditorColor::MatchingBracket:
			return {"matchingBracket", "Matching bracket"};
		case EditorColor::UnmatchedBracket:
			return {"unmatchedBracket", "Unmatched bracket"};
		case EditorColor::DiagnosticError:
			return {"diagnosticError", "Diagnostic error"};
		case EditorColor::DiagnosticWarning:
			return {"diagnosticWarning", "Diagnostic warning"};
		case EditorColor::Count:
			break;
		}

		return {};
	}

	ColorNames NamesOf(ChromeColor color)
	{
		switch (color)
		{
		case ChromeColor::Background:
			return {"background", "Background"};
		case ChromeColor::Surface:
			return {"surface", "Surface"};
		case ChromeColor::SurfaceHovered:
			return {"surfaceHovered", "Surface (hovered)"};
		case ChromeColor::SurfaceActive:
			return {"surfaceActive", "Surface (active)"};
		case ChromeColor::Accent:
			return {"accent", "Accent"};
		case ChromeColor::Text:
			return {"text", "Text"};
		case ChromeColor::TextDisabled:
			return {"textDisabled", "Text (disabled)"};
		case ChromeColor::Border:
			return {"border", "Border"};
		case ChromeColor::Count:
			break;
		}

		return {};
	}

	ColorNames NamesOf(GitColor color)
	{
		switch (color)
		{
		case GitColor::DiffAddedLine:
			return {"diffAddedLine", "Diff added line"};
		case GitColor::DiffRemovedLine:
			return {"diffRemovedLine", "Diff removed line"};
		case GitColor::DiffAddedText:
			return {"diffAddedText", "Diff added text"};
		case GitColor::DiffRemovedText:
			return {"diffRemovedText", "Diff removed text"};
		case GitColor::GutterAdded:
			return {"gutterAdded", "Gutter added"};
		case GitColor::GutterModified:
			return {"gutterModified", "Gutter modified"};
		case GitColor::GutterRemoved:
			return {"gutterRemoved", "Gutter removed"};
		case GitColor::GraphLane1:
			return {"graphLane1", "Graph lane 1"};
		case GitColor::GraphLane2:
			return {"graphLane2", "Graph lane 2"};
		case GitColor::GraphLane3:
			return {"graphLane3", "Graph lane 3"};
		case GitColor::GraphLane4:
			return {"graphLane4", "Graph lane 4"};
		case GitColor::GraphLane5:
			return {"graphLane5", "Graph lane 5"};
		case GitColor::GraphLane6:
			return {"graphLane6", "Graph lane 6"};
		case GitColor::Count:
			break;
		}

		return {};
	}

	ColorNames NamesOf(SessionColor color)
	{
		switch (color)
		{
		case SessionColor::Read:
			return {"read", "Tool: read"};
		case SessionColor::Edit:
			return {"edit", "Tool: edit"};
		case SessionColor::Run:
			return {"run", "Tool: run"};
		case SessionColor::Agent:
			return {"agent", "Tool: agent"};
		case SessionColor::Web:
			return {"web", "Tool: web"};
		case SessionColor::Other:
			return {"other", "Tool: other"};
		case SessionColor::Failed:
			return {"failed", "Tool: failed"};
		case SessionColor::UserBlock:
			return {"userBlock", "User message"};
		case SessionColor::ClaudeBlock:
			return {"claudeBlock", "Claude reply"};
		case SessionColor::CodeBlock:
			return {"codeBlock", "Code block"};
		case SessionColor::Injected:
			return {"injected", "Injected text"};
		case SessionColor::Count:
			break;
		}

		return {};
	}

	ColorNames NamesOf(LayoutMetric metric)
	{
		switch (metric)
		{
		case LayoutMetric::PanelGap:
			return {"panelGap", "Gap between panels"};
		case LayoutMetric::PanelRounding:
			return {"panelRounding", "Panel corner radius"};
		case LayoutMetric::ControlRounding:
			return {"controlRounding", "Control corner radius"};
		case LayoutMetric::BorderWidth:
			return {"borderWidth", "Border width"};
		case LayoutMetric::ScrollbarWidth:
			return {"scrollbarWidth", "Scrollbar width"};
		case LayoutMetric::Count:
			break;
		}

		return {};
	}

	std::string_view NameOf(TokenKind kind)
	{
		return NamesOf(kind).Key;
	}

	std::string_view NameOf(EditorColor color)
	{
		return NamesOf(color).Key;
	}

	std::string_view NameOf(ChromeColor color)
	{
		return NamesOf(color).Key;
	}

	std::string_view NameOf(GitColor color)
	{
		return NamesOf(color).Key;
	}

	std::string_view NameOf(SessionColor color)
	{
		return NamesOf(color).Key;
	}

	std::string_view NameOf(LayoutMetric metric)
	{
		return NamesOf(metric).Key;
	}

	std::string_view LabelOf(LayoutMetric metric)
	{
		return NamesOf(metric).Label;
	}

	std::string_view LabelOf(TokenKind kind)
	{
		return NamesOf(kind).Label;
	}

	std::string_view LabelOf(EditorColor color)
	{
		return NamesOf(color).Label;
	}

	std::string_view LabelOf(ChromeColor color)
	{
		return NamesOf(color).Label;
	}

	std::string_view LabelOf(GitColor color)
	{
		return NamesOf(color).Label;
	}

	std::string_view LabelOf(SessionColor color)
	{
		return NamesOf(color).Label;
	}

	const Core::Color& ColourFromScheme(const ResolvedScheme& scheme, TokenKind slot)
	{
		return scheme.Syntax[static_cast<std::size_t>(slot)];
	}

	const Core::Color& ColourFromScheme(const ResolvedScheme& scheme, EditorColor slot)
	{
		return scheme.Editor[static_cast<std::size_t>(slot)];
	}

	const Core::Color& ColourFromScheme(const ResolvedScheme& scheme, ChromeColor slot)
	{
		return scheme.Chrome[static_cast<std::size_t>(slot)];
	}

	const Core::Color& ColourFromScheme(const ResolvedScheme& scheme, GitColor slot)
	{
		return scheme.Git[static_cast<std::size_t>(slot)];
	}

	const Core::Color& ColourFromScheme(const ResolvedScheme& scheme, SessionColor slot)
	{
		return scheme.Session[static_cast<std::size_t>(slot)];
	}

	ResolvedScheme ResolveSchemeOverBase(const ColorScheme& scheme, const ResolvedScheme& base)
	{
		ResolvedScheme resolved = base;

		for (std::size_t slotIndex = 0; slotIndex < scheme.Syntax.size(); ++slotIndex)
		{
			if (scheme.Syntax[slotIndex])
			{
				resolved.Syntax[slotIndex] = *scheme.Syntax[slotIndex];
			}
		}

		for (std::size_t slotIndex = 0; slotIndex < scheme.Editor.size(); ++slotIndex)
		{
			if (scheme.Editor[slotIndex])
			{
				resolved.Editor[slotIndex] = *scheme.Editor[slotIndex];
			}
		}

		for (std::size_t slotIndex = 0; slotIndex < scheme.Chrome.size(); ++slotIndex)
		{
			if (scheme.Chrome[slotIndex])
			{
				resolved.Chrome[slotIndex] = *scheme.Chrome[slotIndex];
			}
		}

		for (std::size_t slotIndex = 0; slotIndex < scheme.Git.size(); ++slotIndex)
		{
			if (scheme.Git[slotIndex])
			{
				resolved.Git[slotIndex] = *scheme.Git[slotIndex];
			}
		}

		for (std::size_t slotIndex = 0; slotIndex < scheme.Session.size(); ++slotIndex)
		{
			if (scheme.Session[slotIndex])
			{
				resolved.Session[slotIndex] = *scheme.Session[slotIndex];
			}
		}

		for (std::size_t metricIndex = 0; metricIndex < scheme.Layout.size(); ++metricIndex)
		{
			if (scheme.Layout[metricIndex])
			{
				resolved.Layout[metricIndex] = *scheme.Layout[metricIndex];
			}
		}

		return resolved;
	}
}
