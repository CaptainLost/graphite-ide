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

	// WARNING: A grammar given a server's declaration alone reads it as a statement: in C#, `void Console.WriteLine()` becomes a
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
	};

	const LanguageDefinition& DefinitionOf(LanguageId language);

	bool LanguageHasServers(LanguageId language);

	std::span<const LanguageId> LanguagesWithServers();

	LanguageId LanguageOfPath(const std::filesystem::path& path);

	std::string_view LanguageDisplayName(LanguageId language);

	SyntaxSource SyntaxOf(LanguageId language);

	const Lexer* LexerFor(LanguageId language);

	std::optional<std::string_view> LineCommentPrefix(LanguageId language);
}
