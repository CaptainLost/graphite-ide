#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Api/TokenKind.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/Token.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

inline std::vector<Graphite::Token> LexCppLine(std::string_view line)
{
	std::vector<Graphite::Token> tokens;
	Graphite::LexerFor(Graphite::LanguageId::Cpp)->LexLine(line, Graphite::LineState{}, tokens);
	return tokens;
}

inline std::string_view TokenKindWord(Graphite::TokenKind kind)
{
	switch (kind)
	{
	case Graphite::TokenKind::Text:
		return "text";
	case Graphite::TokenKind::Keyword:
		return "keyword";
	case Graphite::TokenKind::Identifier:
		return "identifier";
	case Graphite::TokenKind::Number:
		return "number";
	case Graphite::TokenKind::String:
		return "string";
	case Graphite::TokenKind::Character:
		return "character";
	case Graphite::TokenKind::Comment:
		return "comment";
	case Graphite::TokenKind::Preprocessor:
		return "preprocessor";
	case Graphite::TokenKind::Punctuation:
		return "punctuation";
	case Graphite::TokenKind::Invalid:
		return "invalid";
	case Graphite::TokenKind::SemanticType:
		return "type";
	case Graphite::TokenKind::SemanticFunction:
		return "function";
	case Graphite::TokenKind::SemanticMacro:
		return "macro";
	case Graphite::TokenKind::SemanticMember:
		return "member";
	case Graphite::TokenKind::SemanticParameter:
		return "parameter";
	case Graphite::TokenKind::SemanticNamespace:
		return "namespace";
	case Graphite::TokenKind::Count:
		return "count";
	}

	return "unknown";
}

inline std::string DescribeTokens(std::string_view line, const std::vector<Graphite::Token>& tokens)
{
	std::string description;

	for (const Graphite::Token& token : tokens)
	{
		if (!description.empty())
		{
			description += " | ";
		}

		description += TokenKindWord(token.Kind);
		description += " '";
		description += line.substr(token.From, token.To - token.From);
		description += "'";
	}

	return description;
}

inline std::string_view FixtureRow(std::string_view text, std::size_t row)
{
	std::size_t rowStart = 0;

	for (std::size_t skippedRow = 0; skippedRow < row; ++skippedRow)
	{
		const std::size_t newline = text.find('\n', rowStart);

		if (newline == std::string_view::npos)
		{
			return {};
		}

		rowStart = newline + 1;
	}

	const std::size_t rowEnd = text.find('\n', rowStart);

	return rowEnd == std::string_view::npos ? text.substr(rowStart) : text.substr(rowStart, rowEnd - rowStart);
}

inline std::string ManyCSharpClasses(std::size_t count)
{
	std::string text;

	for (std::size_t classIndex = 0; classIndex < count; ++classIndex)
	{
		text += "class C" + std::to_string(classIndex) + " { int f = 1; void M() { f += 2; } }\n";
	}

	return text;
}

inline std::string TenThousandLinesOfCSharp()
{
	constexpr std::size_t kMembers = 3300;
	std::string text = "namespace Demo;\npublic class Big\n{\n";

	for (std::size_t memberIndex = 0; memberIndex < kMembers; ++memberIndex)
	{
		const std::string number = std::to_string(memberIndex);
		text += "    public int Field" + number + " = " + number + ";\n";
		text += "    public void Method" + number + "(string label) { System.Console.WriteLine($\"{label}: {Field" + number + "}\"); }\n";
		text += "    // member " + number + "\n";
	}

	text += "}\n";
	return text;
}

inline double MillisecondsSince(std::chrono::steady_clock::time_point start)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

inline constexpr std::string_view kCSharpFixture = R"cs(using System.Collections.Generic;
namespace Demo.App;
/* multi
   line */
[Serializable]
public sealed class Widget<T> : IDisposable where T : class
{
    private int count = 0x1F;
    public string Name { get; init; } = @"C:\path";
    public Widget(int size) { count = size; }
    public void Run(string label)
    {
        var s = $"{label}: {count,5}";
        var raw = """
          raw "text"
          """;
        char c = '\n';
        if (label is null) return;
        Console.WriteLine(s + raw + c);
    }
#if DEBUG
    void Trace() { }
#endif
#region Helpers
#endregion
}
)cs";
