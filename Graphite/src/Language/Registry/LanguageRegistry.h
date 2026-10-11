#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Syntax/SyntaxSource.h"
#include "Language/Syntax/Token.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace Graphite {

	enum class SyntaxKind : std::uint8_t
	{
		None,
		CppLexer,
		CSharpGrammar
	};

	// A grammar given a server's declaration alone reads it as a statement: in C#, `void Console.WriteLine()` becomes a
	// local function named Console. Set inside these rows, it reads as the member it declares.
	struct MemberContext
	{
		std::string_view Opening;
		std::string_view Closing;
		std::span<const std::string_view> TopLevelKeywords;
	};

	struct LanguageDefinition
	{
		LanguageId Id = LanguageId::PlainText;
		std::string_view SettingsName;
		std::string_view DisplayName;
		std::span<const std::string_view> Extensions;
		std::string_view LineComment;
		SyntaxKind Syntax = SyntaxKind::None;
		MemberContext DeclarationContext;
		std::span<const std::string_view> Servers;
		// Set for C++ alone, until clangd stops shipping: a settings file written before languages had
		// a choice stores none, and C++ must keep the server it always had.
		bool UsesRecommendedServerUntilChosen = false;
	};

	const LanguageDefinition& DefinitionOf(LanguageId language);

	bool LanguageHasServers(LanguageId language);

	std::span<const LanguageId> LanguagesWithServers();

	LanguageId LanguageOfPath(const std::filesystem::path& path);

	// Empty for PlainText, which is the absence of a recognised language rather than one.
	std::string_view LanguageDisplayName(LanguageId language);

	SyntaxSource SyntaxOf(LanguageId language);

	// Stateless and shared; its lifetime is the program's.
	const Lexer* LexerFor(LanguageId language);

	std::optional<std::string_view> LineCommentPrefix(LanguageId language);
}
