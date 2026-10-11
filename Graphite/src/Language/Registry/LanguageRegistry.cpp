#include "Language/Registry/LanguageRegistry.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Language/Syntax/CppLexer.h"
#include "Language/Syntax/Grammars/CSharpGrammar.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Graphite {

	namespace {

		constexpr std::array<std::string_view, 7> kCppExtensions{".cpp", ".cc", ".cxx", ".h", ".hpp", ".hxx", ".ipp"};
		constexpr std::array<std::string_view, 1> kCSharpExtensions{".cs"};
		constexpr std::array<std::string_view, 1> kTypeScriptExtensions{".ts"};

		constexpr std::array<std::string_view, 1> kCppServers{"clangd"};
		constexpr std::array<std::string_view, 3> kCSharpServers{"roslyn", "csharp-ls", "omnisharp"};

		constexpr std::string_view kSlashSlashComment = "//";

		constexpr std::array<std::string_view, 7> kCSharpTopLevelKeywords{"namespace", "class", "struct", "interface", "enum", "record", "delegate"};

		// The closing row's ';' ends a method declared without a body, which would otherwise end in a missing token.
		constexpr MemberContext kCSharpMemberContext{.Opening = "class Declared {", .Closing = ";}", .TopLevelKeywords = kCSharpTopLevelKeywords};

		constexpr std::array<LanguageDefinition, kAllLanguages.size()> kDefinitions{{
			{.Id = LanguageId::PlainText},
			{.Id = LanguageId::Cpp,
			 .SettingsName = "Cpp",
			 .DisplayName = "C++",
			 .Extensions = kCppExtensions,
			 .LineComment = kSlashSlashComment,
			 .Syntax = SyntaxKind::CppLexer,
			 .Servers = kCppServers,
			 .UsesRecommendedServerUntilChosen = true},
			{.Id = LanguageId::CSharp,
			 .SettingsName = "CSharp",
			 .DisplayName = "C#",
			 .Extensions = kCSharpExtensions,
			 .LineComment = kSlashSlashComment,
			 .Syntax = SyntaxKind::CSharpGrammar,
			 .DeclarationContext = kCSharpMemberContext,
			 .Servers = kCSharpServers},
			{.Id = LanguageId::TypeScript,
			 .SettingsName = "TypeScript",
			 .DisplayName = "TypeScript",
			 .Extensions = kTypeScriptExtensions,
			 .LineComment = kSlashSlashComment},
		}};

		consteval bool EveryDefinitionSitsAtItsOwnId()
		{
			for (std::size_t definitionIndex = 0; definitionIndex < kDefinitions.size(); ++definitionIndex)
			{
				if (static_cast<std::size_t>(kDefinitions[definitionIndex].Id) != definitionIndex)
				{
					return false;
				}
			}

			return true;
		}

		static_assert(EveryDefinitionSitsAtItsOwnId(), "kDefinitions must list the languages in LanguageId order");

		bool HasExtension(const LanguageDefinition& definition, std::string_view extension)
		{
			for (const std::string_view candidate : definition.Extensions)
			{
				if (candidate == extension)
				{
					return true;
				}
			}

			return false;
		}

		std::vector<LanguageId> CollectLanguagesWithServers()
		{
			std::vector<LanguageId> languages;

			for (const LanguageDefinition& definition : kDefinitions)
			{
				if (!definition.Servers.empty())
				{
					languages.push_back(definition.Id);
				}
			}

			return languages;
		}
	}

	const LanguageDefinition& DefinitionOf(LanguageId language)
	{
		return kDefinitions[static_cast<std::size_t>(language)];
	}

	bool LanguageHasServers(LanguageId language)
	{
		return !DefinitionOf(language).Servers.empty();
	}

	std::span<const LanguageId> LanguagesWithServers()
	{
		static const std::vector<LanguageId> languagesWithServers = CollectLanguagesWithServers();

		return languagesWithServers;
	}

	LanguageId LanguageOfPath(const std::filesystem::path& path)
	{
		// A name whose only period is its first character has no extension at all, so ".gitignore" falls through here.
		if (!path.has_extension())
		{
			return LanguageId::PlainText;
		}

		const std::string extension = Core::LoweredExtension(path);

		for (const LanguageDefinition& definition : kDefinitions)
		{
			if (HasExtension(definition, extension))
			{
				return definition.Id;
			}
		}

		return LanguageId::PlainText;
	}

	std::string_view LanguageDisplayName(LanguageId language)
	{
		return DefinitionOf(language).DisplayName;
	}

	SyntaxSource SyntaxOf(LanguageId language)
	{
		// Built on first use, not during static initialisation.
		static const CppLexer cpp;

		switch (DefinitionOf(language).Syntax)
		{
		case SyntaxKind::CppLexer:
			return SyntaxSource{.LineLexer = &cpp};
		case SyntaxKind::CSharpGrammar:
			return SyntaxSource{.TreeGrammar = &CSharpGrammar()};
		case SyntaxKind::None:
			return SyntaxSource{};
		}

		return SyntaxSource{};
	}

	const Lexer* LexerFor(LanguageId language)
	{
		return SyntaxOf(language).LineLexer;
	}

	std::optional<std::string_view> LineCommentPrefix(LanguageId language)
	{
		const std::string_view comment = DefinitionOf(language).LineComment;

		if (comment.empty())
		{
			return std::nullopt;
		}

		return comment;
	}
}
