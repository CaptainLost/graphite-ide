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

	struct LanguageDefinition
	{
		LanguageId Id = LanguageId::PlainText;
		std::string_view SettingsName;
		std::string_view DisplayName;
		std::span<const std::string_view> Extensions;
		std::string_view LineComment;
		SyntaxKind Syntax = SyntaxKind::None;
		std::span<const std::string_view> Servers;
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
