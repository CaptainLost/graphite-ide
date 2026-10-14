#include "Language/Syntax/CppLexer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		enum class Continuation : std::uint32_t
		{
			None,
			BlockComment,
			LineComment,
			String,
			Character,
			RawString
		};

		LineState StateOf(Continuation kind, std::string delimiter = {})
		{
			return LineState{static_cast<std::uint32_t>(kind), std::move(delimiter)};
		}

		Continuation ContinuationOf(const LineState& state)
		{
			return static_cast<Continuation>(state.Mode);
		}

		constexpr std::array kKeywords{
			std::string_view{"alignas"},
			std::string_view{"alignof"},
			std::string_view{"auto"},
			std::string_view{"bool"},
			std::string_view{"break"},
			std::string_view{"case"},
			std::string_view{"catch"},
			std::string_view{"char"},
			std::string_view{"class"},
			std::string_view{"const"},
			std::string_view{"consteval"},
			std::string_view{"constexpr"},
			std::string_view{"constinit"},
			std::string_view{"continue"},
			std::string_view{"decltype"},
			std::string_view{"default"},
			std::string_view{"delete"},
			std::string_view{"do"},
			std::string_view{"double"},
			std::string_view{"else"},
			std::string_view{"enum"},
			std::string_view{"explicit"},
			std::string_view{"export"},
			std::string_view{"extern"},
			std::string_view{"false"},
			std::string_view{"final"},
			std::string_view{"float"},
			std::string_view{"for"},
			std::string_view{"friend"},
			std::string_view{"goto"},
			std::string_view{"if"},
			std::string_view{"inline"},
			std::string_view{"int"},
			std::string_view{"long"},
			std::string_view{"mutable"},
			std::string_view{"namespace"},
			std::string_view{"new"},
			std::string_view{"noexcept"},
			std::string_view{"nullptr"},
			std::string_view{"operator"},
			std::string_view{"override"},
			std::string_view{"private"},
			std::string_view{"protected"},
			std::string_view{"public"},
			std::string_view{"return"},
			std::string_view{"short"},
			std::string_view{"signed"},
			std::string_view{"sizeof"},
			std::string_view{"static"},
			std::string_view{"static_assert"},
			std::string_view{"static_cast"},
			std::string_view{"struct"},
			std::string_view{"switch"},
			std::string_view{"template"},
			std::string_view{"this"},
			std::string_view{"throw"},
			std::string_view{"true"},
			std::string_view{"try"},
			std::string_view{"typedef"},
			std::string_view{"typename"},
			std::string_view{"union"},
			std::string_view{"unsigned"},
			std::string_view{"using"},
			std::string_view{"virtual"},
			std::string_view{"void"},
			std::string_view{"volatile"},
			std::string_view{"while"},
		};

		static_assert(std::is_sorted(kKeywords.begin(), kKeywords.end()), "the keyword lookup is a binary search");

		constexpr unsigned char kFirstNonAsciiByte = 0x80;
		constexpr std::size_t kLineStart = 0;
		constexpr std::size_t kLongestRawDelimiter = 16;

		bool IsDigit(char character)
		{
			return character >= '0' && character <= '9';
		}

		bool IsIdentifierStart(char character)
		{
			const bool isLetter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
			const bool isNonAscii = static_cast<unsigned char>(character) >= kFirstNonAsciiByte;

			return character == '_' || isLetter || isNonAscii;
		}

		bool IsIdentifierPart(char character)
		{
			return IsIdentifierStart(character) || IsDigit(character);
		}

		bool IsBlank(char character)
		{
			return character == ' ' || character == '\t' || character == '\r';
		}

		bool IsExponentMarker(char character)
		{
			return character == 'e' || character == 'E' || character == 'p' || character == 'P';
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

		// WARNING: Deliberately looser than a valid literal: it takes the exponent's sign and the digit separators,
		// so 1e-9 and 1'000ULL are one token each. Colouring wants the span, not the value.
		std::size_t ScanNumber(std::string_view line, std::size_t at)
		{
			++at;

			while (at < line.size())
			{
				const char character = line[at];
				const bool isSignOfExponent = (character == '+' || character == '-') && IsExponentMarker(line[at - 1]);

				if (isSignOfExponent)
				{
					++at;
					continue;
				}

				if (IsIdentifierPart(character) || character == '.' || character == '\'')
				{
					++at;
					continue;
				}

				break;
			}

			return at;
		}

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

		enum class QuoteEnd
		{
			Closed,
			Continued,
			Unterminated
		};

		QuoteEnd ScanQuoted(std::string_view line, std::size_t& at, char quote)
		{
			while (at < line.size())
			{
				if (line[at] == '\\')
				{
					const bool backslashIsLast = at + 1 >= line.size();

					if (backslashIsLast)
					{
						at = line.size();
						return QuoteEnd::Continued;
					}

					at += 2;
					continue;
				}

				if (line[at] == quote)
				{
					++at;
					return QuoteEnd::Closed;
				}

				++at;
			}

			return QuoteEnd::Unterminated;
		}

		bool ScanRawOpening(std::string_view line, std::size_t& at, std::string& delimiter)
		{
			const std::size_t from = at;

			// WARNING: The bound is tested before the increment, so `<=` would accept a delimiter one over the limit.
			while (at < line.size() && line[at] != '(' && at - from < kLongestRawDelimiter)
			{
				++at;
			}

			if (at >= line.size() || line[at] != '(')
			{
				return false;
			}

			delimiter.assign(line.substr(from, at - from));
			++at;
			return true;
		}

		bool ScanRawBody(std::string_view line, std::size_t& at, std::string_view delimiter)
		{
			const std::string closing = std::string(")") + std::string(delimiter) + "\"";
			const std::size_t found = line.find(closing, at);

			if (found == std::string_view::npos)
			{
				at = line.size();
				return false;
			}

			at = found + closing.size();
			return true;
		}

		bool IsLiteralPrefix(std::string_view word)
		{
			return word == "u8" || word == "u" || word == "U" || word == "L" || word == "R" ||
				   word == "u8R" || word == "uR" || word == "UR" || word == "LR";
		}
	}

	LineState CppLexer::LexLine(std::string_view line, const LineState& entry,
								std::vector<Token>& out) const
	{
		LineState state = entry;
		std::size_t at = 0;

		const Continuation carried = ContinuationOf(entry);

		if (carried == Continuation::BlockComment)
		{
			if (!ScanBlockComment(line, at, kLineStart, out))
			{
				return state;
			}

			state = LineState{};
		}
		else if (carried == Continuation::LineComment)
		{
			Emit(out, TokenKind::Comment, kLineStart, line.size());

			return EndsWithContinuation(line) ? state : LineState{};
		}
		else if (carried == Continuation::RawString)
		{
			const bool closed = ScanRawBody(line, at, state.Delimiter);
			Emit(out, TokenKind::String, kLineStart, at);

			if (!closed)
			{
				return state;
			}

			state = LineState{};
		}
		else if (carried == Continuation::String || carried == Continuation::Character)
		{
			const char quote = carried == Continuation::String ? '"' : '\'';
			const TokenKind kind = quote == '"' ? TokenKind::String : TokenKind::Character;
			const QuoteEnd quoteEnd = ScanQuoted(line, at, quote);

			Emit(out, quoteEnd == QuoteEnd::Unterminated ? TokenKind::Invalid : kind, kLineStart, at);

			if (quoteEnd == QuoteEnd::Continued)
			{
				return state;
			}

			state = LineState{};
		}

		while (at < line.size())
		{
			const char character = line[at];

			if (IsBlank(character))
			{
				++at;
				continue;
			}

			if (character == '/' && at + 1 < line.size() && line[at + 1] == '/')
			{
				Emit(out, TokenKind::Comment, at, line.size());

				return EndsWithContinuation(line) ? StateOf(Continuation::LineComment)
												  : LineState{};
			}

			if (character == '/' && at + 1 < line.size() && line[at + 1] == '*')
			{
				const std::size_t from = at;
				at += 2;

				if (!ScanBlockComment(line, at, from, out))
				{
					return StateOf(Continuation::BlockComment);
				}

				continue;
			}

			const bool isFirstTokenOnLine = out.empty();

			if (character == '#' && isFirstTokenOnLine && state == LineState{})
			{
				const std::size_t from = at;
				++at;

				while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
				{
					++at;
				}

				const std::size_t nameFrom = at;

				while (at < line.size() && IsIdentifierPart(line[at]))
				{
					++at;
				}

				Emit(out, TokenKind::Preprocessor, from, at);

				const bool isInclude = line.substr(nameFrom, at - nameFrom) == "include";

				if (isInclude)
				{
					const std::size_t openingAngle = line.find('<', at);
					const std::size_t closingAngle = openingAngle == std::string_view::npos
														 ? std::string_view::npos
														 : line.find('>', openingAngle);

					if (closingAngle != std::string_view::npos)
					{
						Emit(out, TokenKind::String, openingAngle, closingAngle + 1);
						at = closingAngle + 1;
					}
				}

				continue;
			}

			if (IsIdentifierStart(character))
			{
				const std::size_t from = at;

				while (at < line.size() && IsIdentifierPart(line[at]))
				{
					++at;
				}

				const std::string_view word = line.substr(from, at - from);

				const bool rawPrefix = !word.empty() && word.back() == 'R';
				const bool quoteFollows = at < line.size() && (line[at] == '"' || (line[at] == '\'' && !rawPrefix));

				if (quoteFollows && IsLiteralPrefix(word))
				{
					++at;

					if (rawPrefix)
					{
						std::string delimiter;

						if (!ScanRawOpening(line, at, delimiter))
						{
							Emit(out, TokenKind::Invalid, from, line.size());
							return LineState{};
						}

						if (!ScanRawBody(line, at, delimiter))
						{
							Emit(out, TokenKind::String, from, line.size());
							return StateOf(Continuation::RawString, delimiter);
						}

						Emit(out, TokenKind::String, from, at);
						continue;
					}

					const char quote = line[at - 1];
					const TokenKind kind = quote == '"' ? TokenKind::String : TokenKind::Character;
					const QuoteEnd quoteEnd = ScanQuoted(line, at, quote);

					Emit(out, quoteEnd == QuoteEnd::Unterminated ? TokenKind::Invalid : kind, from, at);

					if (quoteEnd == QuoteEnd::Continued)
					{
						return StateOf(quote == '"' ? Continuation::String : Continuation::Character);
					}

					continue;
				}

				Emit(out, IsKeyword(word) ? TokenKind::Keyword : TokenKind::Identifier, from, at);
				continue;
			}

			const bool dotBeforeDigit = character == '.' && at + 1 < line.size() && IsDigit(line[at + 1]);

			if (IsDigit(character) || dotBeforeDigit)
			{
				const std::size_t from = at;
				at = ScanNumber(line, at);
				Emit(out, TokenKind::Number, from, at);
				continue;
			}

			if (character == '"' || character == '\'')
			{
				const std::size_t from = at;
				++at;

				const TokenKind kind = character == '"' ? TokenKind::String : TokenKind::Character;
				const QuoteEnd quoteEnd = ScanQuoted(line, at, character);

				Emit(out, quoteEnd == QuoteEnd::Unterminated ? TokenKind::Invalid : kind, from, at);

				if (quoteEnd == QuoteEnd::Continued)
				{
					return StateOf(character == '"' ? Continuation::String : Continuation::Character);
				}

				continue;
			}

			Emit(out, TokenKind::Punctuation, at, at + 1);
			++at;
		}

		return state;
	}
}
