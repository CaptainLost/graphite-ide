#include "Language/Syntax/SyntaxTree.h"

#include "Core/Base/Narrow.h"
#include "Language/Syntax/Grammar.h"

#include <tree_sitter/api.h>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace Graphite {

	namespace {

		struct RowCapture
		{
			std::size_t From = 0;
			std::size_t To = 0;
			TokenKind Kind = TokenKind::Text;
			std::uint32_t NodeWidth = 0;
			std::uint32_t Pattern = 0;
		};

		const char* ReadText(void* payload, std::uint32_t byteIndex, TSPoint, std::uint32_t* bytesRead)
		{
			const std::string& text = *static_cast<const std::string*>(payload);

			if (byteIndex >= text.size())
			{
				*bytesRead = 0;
				return "";
			}

			*bytesRead = Core::Narrow<std::uint32_t>(text.size() - byteIndex);
			return text.data() + byteIndex;
		}

		bool IsPastDeadline(TSParseState* state)
		{
			const std::chrono::steady_clock::time_point& deadline =
				*static_cast<const std::chrono::steady_clock::time_point*>(state->payload);

			return std::chrono::steady_clock::now() >= deadline;
		}

		std::vector<std::size_t> RowStartsOf(std::string_view text)
		{
			std::vector<std::size_t> rowStarts{0};

			for (std::size_t byteIndex = 0; byteIndex < text.size(); ++byteIndex)
			{
				if (text[byteIndex] == '\n')
				{
					rowStarts.push_back(byteIndex + 1);
				}
			}

			return rowStarts;
		}

		TSPoint PointInRows(const std::vector<std::size_t>& rowStarts, std::size_t byteOffset)
		{
			const std::vector<std::size_t>::const_iterator firstRowAfter =
				std::upper_bound(rowStarts.begin(), rowStarts.end(), byteOffset);
			const std::size_t row = static_cast<std::size_t>(firstRowAfter - rowStarts.begin()) - 1;

			return TSPoint{.row = Core::Narrow<std::uint32_t>(row), .column = Core::Narrow<std::uint32_t>(byteOffset - rowStarts[row])};
		}

		TSPoint PointAfter(std::string_view text, std::size_t fromByte, TSPoint fromPoint, std::size_t toByte)
		{
			TSPoint point = fromPoint;

			for (std::size_t byteIndex = fromByte; byteIndex < toByte; ++byteIndex)
			{
				if (text[byteIndex] == '\n')
				{
					++point.row;
					point.column = 0;
				}
				else
				{
					++point.column;
				}
			}

			return point;
		}

		std::size_t CommonPrefixLength(std::string_view before, std::string_view after)
		{
			const std::size_t shorterLength = std::min(before.size(), after.size());
			std::size_t prefixLength = 0;

			while (prefixLength < shorterLength && before[prefixLength] == after[prefixLength])
			{
				++prefixLength;
			}

			return prefixLength;
		}

		std::size_t CommonSuffixLength(std::string_view before, std::string_view after, std::size_t prefixLength)
		{
			const std::size_t longestSuffix = std::min(before.size(), after.size()) - prefixLength;
			std::size_t suffixLength = 0;

			while (suffixLength < longestSuffix &&
				   before[before.size() - 1 - suffixLength] == after[after.size() - 1 - suffixLength])
			{
				++suffixLength;
			}

			return suffixLength;
		}

		// One edit spanning everything between the common prefix and the common suffix: however
		// many edits made the new text, the tree only has to be told which middle changed.
		TSInputEdit EditBetween(std::string_view before, const std::vector<std::size_t>& beforeRowStarts,
								std::string_view after)
		{
			const std::size_t prefixLength = CommonPrefixLength(before, after);
			const std::size_t suffixLength = CommonSuffixLength(before, after, prefixLength);
			const std::size_t oldEnd = before.size() - suffixLength;
			const std::size_t newEnd = after.size() - suffixLength;
			const TSPoint startPoint = PointInRows(beforeRowStarts, prefixLength);

			return TSInputEdit{.start_byte = Core::Narrow<std::uint32_t>(prefixLength),
							   .old_end_byte = Core::Narrow<std::uint32_t>(oldEnd),
							   .new_end_byte = Core::Narrow<std::uint32_t>(newEnd),
							   .start_point = startPoint,
							   .old_end_point = PointInRows(beforeRowStarts, oldEnd),
							   .new_end_point = PointAfter(after, prefixLength, startPoint, newEnd)};
		}

		bool IsPaintedEarlier(const RowCapture& left, const RowCapture& right)
		{
			if (left.NodeWidth != right.NodeWidth)
			{
				return left.NodeWidth > right.NodeWidth;
			}

			return left.Pattern < right.Pattern;
		}

		// Painted widest first, so a narrower node overwrites the one around it and, for one node,
		// a later pattern overwrites an earlier one. The sort is stable, so ties keep the query's order.
		void AppendPaintedTokens(std::vector<RowCapture>& captures, std::size_t rowLength, std::vector<Token>& out)
		{
			std::stable_sort(captures.begin(), captures.end(), IsPaintedEarlier);

			constexpr std::size_t kUnpainted = std::numeric_limits<std::size_t>::max();
			std::vector<std::size_t> painterOfByte(rowLength, kUnpainted);

			for (std::size_t captureIndex = 0; captureIndex < captures.size(); ++captureIndex)
			{
				for (std::size_t byteIndex = captures[captureIndex].From; byteIndex < captures[captureIndex].To; ++byteIndex)
				{
					painterOfByte[byteIndex] = captureIndex;
				}
			}

			std::size_t runStart = 0;

			while (runStart < rowLength)
			{
				const std::size_t painter = painterOfByte[runStart];
				std::size_t runEnd = runStart + 1;

				while (runEnd < rowLength && painterOfByte[runEnd] == painter)
				{
					++runEnd;
				}

				if (painter != kUnpainted)
				{
					out.push_back(Token{.Kind = captures[painter].Kind, .From = runStart, .To = runEnd});
				}

				runStart = runEnd;
			}
		}
	}

	SyntaxTree::SyntaxTree(const Grammar& grammar)
		: m_Grammar(grammar), m_Parser(ts_parser_new()), m_Cursor(ts_query_cursor_new())
	{
		// The runtime accepts a null language, and a parse with none never finishes.
		m_LanguageAccepted = grammar.GetLanguage() != nullptr && ts_parser_set_language(m_Parser, grammar.GetLanguage());
	}

	SyntaxTree::~SyntaxTree()
	{
		Forget();
		ts_query_cursor_delete(m_Cursor);
		ts_parser_delete(m_Parser);
	}

	void SyntaxTree::Retarget(std::string text)
	{
		if (!m_LanguageAccepted || text.size() > kLargestParsedText)
		{
			Forget();
			return;
		}

		const bool alreadyDescribed = !m_Parsing && m_Tree != nullptr && text == m_Text;

		if (alreadyDescribed)
		{
			return;
		}

		// A parse cancelled by its budget would otherwise resume against the previous target.
		ts_parser_reset(m_Parser);
		DropEditedTree();

		if (m_Tree != nullptr)
		{
			const TSInputEdit edit = EditBetween(m_Text, m_RowStarts, text);
			m_EditedTree = ts_tree_copy(m_Tree);
			ts_tree_edit(m_EditedTree, &edit);
		}

		m_PendingText = std::move(text);
		m_Parsing = true;
	}

	bool SyntaxTree::Advance(std::chrono::microseconds budget)
	{
		return ParsePending(budget);
	}

	bool SyntaxTree::AdvanceToEnd()
	{
		return ParsePending(std::nullopt);
	}

	bool SyntaxTree::ParsePending(std::optional<std::chrono::microseconds> budget)
	{
		if (!m_Parsing)
		{
			return true;
		}

		const TSInput input{.payload = &m_PendingText, .read = ReadText, .encoding = TSInputEncodingUTF8, .decode = nullptr};
		TSTree* parsed = nullptr;

		if (budget.has_value())
		{
			std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + *budget;
			const TSParseOptions options{.payload = &deadline, .progress_callback = IsPastDeadline};

			parsed = ts_parser_parse_with_options(m_Parser, m_EditedTree, input, options);
		}
		else
		{
			parsed = ts_parser_parse(m_Parser, m_EditedTree, input);
		}

		if (parsed == nullptr)
		{
			return false;
		}

		Adopt(parsed);
		return true;
	}

	void SyntaxTree::Adopt(TSTree* parsed)
	{
		if (m_Tree != nullptr)
		{
			ts_tree_delete(m_Tree);
		}

		DropEditedTree();

		m_Tree = parsed;
		m_Text = std::move(m_PendingText);
		m_PendingText.clear();
		m_RowStarts = RowStartsOf(m_Text);
		m_Parsing = false;
	}

	void SyntaxTree::DropEditedTree()
	{
		if (m_EditedTree != nullptr)
		{
			ts_tree_delete(m_EditedTree);
			m_EditedTree = nullptr;
		}
	}

	void SyntaxTree::Forget()
	{
		if (m_Tree != nullptr)
		{
			ts_tree_delete(m_Tree);
			m_Tree = nullptr;
		}

		DropEditedTree();
		ts_parser_reset(m_Parser);

		m_Text.clear();
		m_RowStarts.clear();
		m_PendingText.clear();
		m_Parsing = false;
	}

	std::size_t SyntaxTree::GetRowCount() const
	{
		return m_Tree == nullptr ? 0 : m_RowStarts.size();
	}

	std::string_view SyntaxTree::RowText(std::size_t row) const
	{
		if (row >= GetRowCount())
		{
			return {};
		}

		const std::size_t rowStart = m_RowStarts[row];
		const bool isLastRow = row + 1 == m_RowStarts.size();
		const std::size_t rowEnd = isLastRow ? m_Text.size() : m_RowStarts[row + 1] - 1;

		return std::string_view(m_Text).substr(rowStart, rowEnd - rowStart);
	}

	void SyntaxTree::AppendRowTokens(std::size_t row, std::vector<Token>& out)
	{
		const TSQuery* highlights = m_Grammar.GetHighlights();

		if (highlights == nullptr || row >= GetRowCount())
		{
			return;
		}

		const std::string_view rowText = RowText(row);
		const std::uint32_t queriedRow = Core::Narrow<std::uint32_t>(row);

		ts_query_cursor_set_point_range(m_Cursor, TSPoint{.row = queriedRow, .column = 0},
										TSPoint{.row = queriedRow + 1, .column = 0});
		ts_query_cursor_exec(m_Cursor, highlights, ts_tree_root_node(m_Tree));

		std::vector<RowCapture> captures;
		TSQueryMatch match{};
		std::uint32_t captureIndex = 0;

		while (ts_query_cursor_next_capture(m_Cursor, &match, &captureIndex))
		{
			const TSQueryCapture& capture = match.captures[captureIndex];
			const std::optional<TokenKind> kind = m_Grammar.KindOfCapture(capture.index);
			const TSPoint nodeStart = ts_node_start_point(capture.node);
			const TSPoint nodeEnd = ts_node_end_point(capture.node);
			const bool touchesRow = nodeStart.row <= queriedRow && nodeEnd.row >= queriedRow;

			if (!kind.has_value() || !touchesRow)
			{
				continue;
			}

			const std::size_t from = nodeStart.row < queriedRow ? 0 : std::min<std::size_t>(nodeStart.column, rowText.size());
			const std::size_t to = nodeEnd.row > queriedRow ? rowText.size() : std::min<std::size_t>(nodeEnd.column, rowText.size());

			if (to <= from)
			{
				continue;
			}

			captures.push_back(RowCapture{.From = from,
										  .To = to,
										  .Kind = *kind,
										  .NodeWidth = ts_node_end_byte(capture.node) - ts_node_start_byte(capture.node),
										  .Pattern = match.pattern_index});
		}

		AppendPaintedTokens(captures, rowText.size(), out);
	}
}
