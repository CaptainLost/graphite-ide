#pragma once

namespace Graphite {

	class Grammar;
	class Lexer;

	// At most one of the two is set; neither means the language draws in plain text.
	struct SyntaxSource
	{
		const Lexer* LineLexer = nullptr;
		const Grammar* TreeGrammar = nullptr;

		friend bool operator==(const SyntaxSource&, const SyntaxSource&) = default;
	};
}
