#pragma once

namespace Graphite {

	class Grammar;
	class Lexer;

	struct SyntaxSource
	{
		const Lexer* LineLexer = nullptr;
		const Grammar* TreeGrammar = nullptr;

		friend bool operator==(const SyntaxSource&, const SyntaxSource&) = default;
	};
}
