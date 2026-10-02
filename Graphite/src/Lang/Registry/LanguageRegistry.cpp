#include "Lang/Registry/LanguageRegistry.h"

#include "Core/Base/Ascii.h"
#include "Core/Fs/FileSystem.h"
#include "Lang/Syntax/CppLexer.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::array<std::pair<std::string_view, LanguageId>, 9> kByExtension{{
			{".cpp", LanguageId::Cpp},
			{".cc", LanguageId::Cpp},
			{".cxx", LanguageId::Cpp},
			{".h", LanguageId::Cpp},
			{".hpp", LanguageId::Cpp},
			{".hxx", LanguageId::Cpp},
			{".ipp", LanguageId::Cpp},
			{".cs", LanguageId::CSharp},
			{".ts", LanguageId::TypeScript},
		}};
	}

	LanguageId LanguageOfPath(const std::filesystem::path& path)
	{
		// This also covers the dotfiles: a name whose only period is its first character has no
		// extension at all by [fs.path.decompose], so ".gitignore" reports the whole name as the
		// stem and an empty extension.
		if (!path.has_extension())
		{
			return LanguageId::PlainText;
		}

		const std::string extension = Core::Lowered(Core::ToUtf8(path.extension()));

		for (const auto& [suffix, language] : kByExtension)
		{
			if (extension == suffix)
			{
				return language;
			}
		}

		return LanguageId::PlainText;
	}

	std::string_view LanguageDisplayName(LanguageId language)
	{
		switch (language)
		{
			case LanguageId::Cpp:
				return "C++";
			case LanguageId::CSharp:
				return "C#";
			case LanguageId::TypeScript:
				return "TypeScript";
			case LanguageId::PlainText:
				return {};
		}

		return {};
	}

	const Lexer* LexerFor(LanguageId language)
	{
		// Stateless, so one instance serves every document. Function-local rather than a file
		// scope object, per ADR 0033 on complex initialization of globals.
		static const CppLexer cpp;

		return language == LanguageId::Cpp ? &cpp : nullptr;
	}

	std::optional<std::string_view> LineCommentPrefix(LanguageId language)
	{
		switch (language)
		{
			case LanguageId::Cpp:
			case LanguageId::CSharp:
			case LanguageId::TypeScript:
				return "//";

			case LanguageId::PlainText:
				return std::nullopt;
		}

		return std::nullopt;
	}
}
