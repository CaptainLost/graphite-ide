#pragma once

#include "Language/Api/TokenKind.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

struct TSLanguage;
struct TSQuery;

namespace Graphite {

	std::optional<TokenKind> KindOfCaptureName(std::string_view captureName);

	class Grammar
	{
	public:
		Grammar(const TSLanguage* language, std::string_view highlightQuery);
		~Grammar();

		Grammar(const Grammar&) = delete;
		Grammar& operator=(const Grammar&) = delete;

		const TSLanguage* GetLanguage() const { return m_Language; }

		// WARNING: Null when the query does not compile against the language; nothing is coloured then.
		const TSQuery* GetHighlights() const { return m_Highlights; }

		std::optional<std::size_t> GetQueryErrorOffset() const { return m_QueryErrorOffset; }

		std::size_t CaptureCount() const { return m_CaptureKinds.size(); }

		std::optional<TokenKind> KindOfCapture(std::uint32_t captureIndex) const;

		std::string_view CaptureName(std::uint32_t captureIndex) const;

	private:
		const TSLanguage* m_Language = nullptr;
		TSQuery* m_Highlights = nullptr;
		std::optional<std::size_t> m_QueryErrorOffset;
		std::vector<std::optional<TokenKind>> m_CaptureKinds;
	};
}
