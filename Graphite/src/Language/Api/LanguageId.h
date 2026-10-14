#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Graphite {

	enum class LanguageId : std::uint8_t
	{
		PlainText,
		Cpp,
		CSharp,
		TypeScript
	};

	inline constexpr std::array<LanguageId, 4> kAllLanguages{
		LanguageId::PlainText, LanguageId::Cpp, LanguageId::CSharp, LanguageId::TypeScript};

	static_assert(kAllLanguages.size() == static_cast<std::size_t>(LanguageId::TypeScript) + 1,
				  "A language was added to LanguageId without being added to kAllLanguages");
}
