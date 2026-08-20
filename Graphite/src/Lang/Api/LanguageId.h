#pragma once

#include <array>
#include <cstddef>
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

	// Every value above, so anything that has to consider all of them - which server serves
	// which, which have lexers - can iterate rather than repeat the list. An enum has no
	// iteration order of its own, and a second hand-written list is a second thing to forget
	// when a language is added.
	inline constexpr std::array<LanguageId, 4> kAllLanguages{
		LanguageId::PlainText, LanguageId::Cpp, LanguageId::CSharp, LanguageId::TypeScript};

	static_assert(kAllLanguages.size() == static_cast<std::size_t>(LanguageId::TypeScript) + 1,
				  "A language was added to LanguageId without being added to kAllLanguages");
}
