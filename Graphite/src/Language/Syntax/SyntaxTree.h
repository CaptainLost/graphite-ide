#pragma once

#include "Language/Syntax/Token.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct TSParser;
struct TSQueryCursor;
struct TSTree;

namespace Graphite {

	class Grammar;

	inline constexpr std::size_t kLargestParsedText = 4 * 1024 * 1024;

	class SyntaxTree
	{
	public:
		explicit SyntaxTree(const Grammar& grammar);
		~SyntaxTree();

		SyntaxTree(const SyntaxTree&) = delete;
		SyntaxTree& operator=(const SyntaxTree&) = delete;

		void Retarget(std::string text);

		bool Advance(std::chrono::microseconds budget);

		bool AdvanceToEnd();

		bool IsParsing() const { return m_Parsing; }

		bool HasTree() const { return m_Tree != nullptr; }

		std::size_t GetRowCount() const;

		std::string_view RowText(std::size_t row) const;

		void AppendRowTokens(std::size_t row, std::vector<Token>& out);

	private:
		bool ParsePending(std::optional<std::chrono::microseconds> budget);
		void Adopt(TSTree* parsed);
		void DropEditedTree();
		void Forget();

		const Grammar& m_Grammar;
		TSParser* m_Parser = nullptr;
		TSQueryCursor* m_Cursor = nullptr;
		bool m_LanguageAccepted = false;

		TSTree* m_Tree = nullptr;
		std::string m_Text;
		std::vector<std::size_t> m_RowStarts;

		TSTree* m_EditedTree = nullptr;
		std::string m_PendingText;
		bool m_Parsing = false;
	};
}
