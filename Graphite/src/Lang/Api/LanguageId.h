#pragma once

#include <cstdint>

namespace Graphite {

	// The languages the application can recognise. A language may be listed here before it
	// has a lexer or a model: recognising a .cs file as C# is what lets the editor say so in
	// the status bar and what makes the registry's lookup total.
	enum class LanguageId : std::uint8_t
	{
		PlainText,
		Cpp,
		CSharp,
		TypeScript
	};
}
