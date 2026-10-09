#include "Language/Syntax/Grammar.h"

#include "Core/Base/Narrow.h"

#include <tree_sitter/api.h>

#include <array>

namespace Graphite {

	namespace {

		struct CaptureRule
		{
			std::string_view Name;
			TokenKind Kind = TokenKind::Text;
		};

		// The first rule that names a capture, or a capture under it after a dot, wins — which is
		// why "variable.parameter" has to come before "variable".
		constexpr std::array<CaptureRule, 15> kCaptureRules{{
			{.Name = "variable.parameter", .Kind = TokenKind::SemanticParameter},
			{.Name = "variable", .Kind = TokenKind::Identifier},
			{.Name = "keyword", .Kind = TokenKind::Keyword},
			{.Name = "string", .Kind = TokenKind::String},
			{.Name = "character", .Kind = TokenKind::Character},
			{.Name = "comment", .Kind = TokenKind::Comment},
			{.Name = "number", .Kind = TokenKind::Number},
			{.Name = "type", .Kind = TokenKind::SemanticType},
			{.Name = "function", .Kind = TokenKind::SemanticFunction},
			{.Name = "property", .Kind = TokenKind::SemanticMember},
			{.Name = "namespace", .Kind = TokenKind::SemanticNamespace},
			{.Name = "module", .Kind = TokenKind::SemanticNamespace},
			{.Name = "preprocessor", .Kind = TokenKind::Preprocessor},
			{.Name = "operator", .Kind = TokenKind::Punctuation},
			{.Name = "punctuation", .Kind = TokenKind::Punctuation},
		}};

		bool RuleCovers(const CaptureRule& rule, std::string_view captureName)
		{
			if (!captureName.starts_with(rule.Name))
			{
				return false;
			}

			const bool isTheRuleItself = captureName.size() == rule.Name.size();

			return isTheRuleItself || captureName[rule.Name.size()] == '.';
		}

		std::string_view NameOfCapture(const TSQuery* query, std::uint32_t captureIndex)
		{
			std::uint32_t nameLength = 0;
			const char* name = ts_query_capture_name_for_id(query, captureIndex, &nameLength);

			return std::string_view(name, nameLength);
		}

		std::vector<std::optional<TokenKind>> KindsOfCaptures(const TSQuery* query)
		{
			std::vector<std::optional<TokenKind>> kinds;
			const std::uint32_t captureCount = ts_query_capture_count(query);

			for (std::uint32_t captureIndex = 0; captureIndex < captureCount; ++captureIndex)
			{
				kinds.push_back(KindOfCaptureName(NameOfCapture(query, captureIndex)));
			}

			return kinds;
		}
	}

	std::optional<TokenKind> KindOfCaptureName(std::string_view captureName)
	{
		for (const CaptureRule& rule : kCaptureRules)
		{
			if (RuleCovers(rule, captureName))
			{
				return rule.Kind;
			}
		}

		return std::nullopt;
	}

	Grammar::Grammar(const TSLanguage* language, std::string_view highlightQuery)
		: m_Language(language)
	{
		std::uint32_t errorOffset = 0;
		TSQueryError errorType = TSQueryErrorNone;

		m_Highlights = ts_query_new(language, highlightQuery.data(), Core::Narrow<std::uint32_t>(highlightQuery.size()),
									&errorOffset, &errorType);

		if (m_Highlights == nullptr)
		{
			m_QueryErrorOffset = errorOffset;
			return;
		}

		m_CaptureKinds = KindsOfCaptures(m_Highlights);
	}

	Grammar::~Grammar()
	{
		if (m_Highlights != nullptr)
		{
			ts_query_delete(m_Highlights);
		}
	}

	std::optional<TokenKind> Grammar::KindOfCapture(std::uint32_t captureIndex) const
	{
		if (captureIndex >= m_CaptureKinds.size())
		{
			return std::nullopt;
		}

		return m_CaptureKinds[captureIndex];
	}

	std::string_view Grammar::CaptureName(std::uint32_t captureIndex) const
	{
		if (m_Highlights == nullptr || captureIndex >= m_CaptureKinds.size())
		{
			return {};
		}

		return NameOfCapture(m_Highlights, captureIndex);
	}
}
