#include "Lang/Syntax/CppLexer.h"

#include <algorithm>
#include <array>

namespace Graphite {

	namespace {

		// Sorted, so the lookup is a binary search. Nothing checks the sortedness at run time —
		// a keyword out of order goes quietly unrecognised — so keep it sorted when adding one.
		constexpr std::array kKeywords{
			std::string_view{"alignas"}, std::string_view{"alignof"},
			std::string_view{"auto"}, std::string_view{"bool"},
			std::string_view{"break"}, std::string_view{"case"},
			std::string_view{"catch"}, std::string_view{"char"},
			std::string_view{"class"}, std::string_view{"const"},
			std::string_view{"consteval"}, std::string_view{"constexpr"},
			std::string_view{"constinit"}, std::string_view{"continue"},
			std::string_view{"decltype"}, std::string_view{"default"},
			std::string_view{"delete"}, std::string_view{"do"},
			std::string_view{"double"}, std::string_view{"else"},
			std::string_view{"enum"}, std::string_view{"explicit"},
			std::string_view{"export"}, std::string_view{"extern"},
			std::string_view{"false"}, std::string_view{"final"},
			std::string_view{"float"}, std::string_view{"for"},
			std::string_view{"friend"}, std::string_view{"goto"},
			std::string_view{"if"}, std::string_view{"inline"},
			std::string_view{"int"}, std::string_view{"long"},
			std::string_view{"mutable"}, std::string_view{"namespace"},
			std::string_view{"new"}, std::string_view{"noexcept"},
			std::string_view{"nullptr"}, std::string_view{"operator"},
			std::string_view{"override"}, std::string_view{"private"},
			std::string_view{"protected"}, std::string_view{"public"},
			std::string_view{"return"}, std::string_view{"short"},
			std::string_view{"signed"}, std::string_view{"sizeof"},
			std::string_view{"static"}, std::string_view{"static_assert"},
			std::string_view{"static_cast"}, std::string_view{"struct"},
			std::string_view{"switch"}, std::string_view{"template"},
			std::string_view{"this"}, std::string_view{"throw"},
			std::string_view{"true"}, std::string_view{"try"},
			std::string_view{"typedef"}, std::string_view{"typename"},
			std::string_view{"union"}, std::string_view{"unsigned"},
			std::string_view{"using"}, std::string_view{"virtual"},
			std::string_view{"void"}, std::string_view{"volatile"},
			std::string_view{"while"},
		};

		bool IsIdentifierStart(char c)
		{
			return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				   static_cast<unsigned char>(c) >= 0x80;
		}

		bool IsIdentifierPart(char c)
		{
			return IsIdentifierStart(c) || (c >= '0' && c <= '9');
		}

		bool IsDigit(char c)
		{
			return c >= '0' && c <= '9';
		}

		bool IsKeyword(std::string_view word)
		{
			return std::binary_search(kKeywords.begin(), kKeywords.end(), word);
		}

		void Emit(std::vector<Token>& out, TokenKind kind, std::size_t from, std::size_t to)
		{
			if (to > from)
			{
				out.push_back(Token{kind, from, to});
			}
		}

		// A pp-number in the standard's sense, which is deliberately looser than a valid literal:
		// it swallows the exponent's sign so 1e-9 is one token rather than three, and it swallows
		// the digit separators and the suffix so 1'000ULL is one too. Colouring wants the span,
		// not the value.
		std::size_t ScanNumber(std::string_view line, std::size_t at)
		{
			++at;

			while (at < line.size())
			{
				const char c = line[at];

				if ((c == '+' || c == '-') &&
					(line[at - 1] == 'e' || line[at - 1] == 'E' || line[at - 1] == 'p' ||
					 line[at - 1] == 'P'))
				{
					++at;
					continue;
				}

				if (IsIdentifierPart(c) || c == '.' || c == '\'')
				{
					++at;
					continue;
				}

				break;
			}

			return at;
		}

		// Consumes to the closing */, emitting one comment token from `from`. Returns true when
		// the comment closed on this line.
		bool ScanBlockComment(std::string_view line, std::size_t& at, std::size_t from,
							  std::vector<Token>& out)
		{
			while (at < line.size())
			{
				if (line[at] == '*' && at + 1 < line.size() && line[at + 1] == '/')
				{
					at += 2;
					Emit(out, TokenKind::Comment, from, at);
					return true;
				}

				++at;
			}

			Emit(out, TokenKind::Comment, from, line.size());
			return false;
		}

		// True when the line ends with an odd number of backslashes, which is what splices it
		// onto the line below.
		bool EndsWithContinuation(std::string_view line)
		{
			std::size_t backslashes = 0;

			while (backslashes < line.size() &&
				   line[line.size() - 1 - backslashes] == '\\')
			{
				++backslashes;
			}

			return backslashes % 2 == 1;
		}
	}

	LineState CppLexer::LexLine(std::string_view line, const LineState& entry,
								std::vector<Token>& out) const
	{
		LineState state = entry;
		std::size_t at = 0;

		if (state.Kind == LineState::Continuation::BlockComment)
		{
			if (!ScanBlockComment(line, at, 0, out))
			{
				return state;
			}

			state = LineState{};
		}
		else if (state.Kind == LineState::Continuation::LineComment)
		{
			Emit(out, TokenKind::Comment, 0, line.size());

			return EndsWithContinuation(line) ? state : LineState{};
		}

		while (at < line.size())
		{
			const char c = line[at];

			if (c == ' ' || c == '\t' || c == '\r')
			{
				++at;
				continue;
			}

			if (c == '/' && at + 1 < line.size() && line[at + 1] == '/')
			{
				Emit(out, TokenKind::Comment, at, line.size());

				return EndsWithContinuation(line) ? LineState{LineState::Continuation::LineComment, {}}
												  : LineState{};
			}

			if (c == '/' && at + 1 < line.size() && line[at + 1] == '*')
			{
				const std::size_t from = at;
				at += 2;

				if (!ScanBlockComment(line, at, from, out))
				{
					return LineState{LineState::Continuation::BlockComment, {}};
				}

				continue;
			}

			if (IsIdentifierStart(c))
			{
				const std::size_t from = at;

				while (at < line.size() && IsIdentifierPart(line[at]))
				{
					++at;
				}

				const std::string_view word = line.substr(from, at - from);
				Emit(out, IsKeyword(word) ? TokenKind::Keyword : TokenKind::Identifier, from, at);
				continue;
			}

			// A dot only starts a number when a digit follows it, so ".5" is a number and "a.b"
			// is punctuation between two identifiers.
			if (IsDigit(c) || (c == '.' && at + 1 < line.size() && IsDigit(line[at + 1])))
			{
				const std::size_t from = at;
				at = ScanNumber(line, at);
				Emit(out, TokenKind::Number, from, at);
				continue;
			}

			Emit(out, TokenKind::Punctuation, at, at + 1);
			++at;
		}

		return state;
	}
}
