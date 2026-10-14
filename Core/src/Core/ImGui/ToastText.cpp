#include "corepch.h"

#include "Core/ImGui/ToastText.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Encoding.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <numeric>
#include <optional>
#include <utility>

namespace Core {

	namespace {
		constexpr unsigned char kFirstNonAsciiByte = 0x80;
		constexpr char kQuote = '\'';
		constexpr char kNewline = '\n';
		constexpr bool kRegularFace = false;
		constexpr float kNoGap = 0.0f;
		constexpr std::size_t kAtLeastOneByte = 1;
		constexpr std::size_t kDrivePrefixLength = 3;
		constexpr std::size_t kDotAndOneCharacterLength = 2;
		constexpr std::array<std::string_view, 6> kRelativePrefixes = {"./", ".\\", "../", "..\\", "~/", "~\\"};

		bool IsSpaceWithinLine(char character)
		{
			return character == ' ' || character == '\t' || character == '\r' || character == '\f' || character == '\v';
		}

		bool IsWordCharacter(char character)
		{
			const auto byte = static_cast<unsigned char>(character);
			return byte >= kFirstNonAsciiByte || std::isalnum(byte) != 0 || character == '_';
		}

		bool ContainsWordCharacter(std::string_view text)
		{
			for (const char character : text)
			{
				if (IsWordCharacter(character))
				{
					return true;
				}
			}

			return false;
		}

		std::optional<std::size_t> ClosingQuoteOfStrongSpan(std::string_view text, std::size_t openIndex)
		{
			if (text[openIndex] != kQuote || (openIndex > 0 && IsWordCharacter(text[openIndex - 1])))
			{
				return std::nullopt;
			}

			const std::size_t firstIndexAfterOneInnerCharacter = openIndex + 2;

			for (std::size_t scanIndex = firstIndexAfterOneInnerCharacter; scanIndex < text.size(); ++scanIndex)
			{
				if (text[scanIndex] == kNewline)
				{
					return std::nullopt;
				}

				const bool endsText = scanIndex + 1 == text.size();

				if (text[scanIndex] == kQuote && (endsText || !IsWordCharacter(text[scanIndex + 1])))
				{
					return scanIndex;
				}
			}

			return std::nullopt;
		}

		bool IsOpeningPunctuation(char character)
		{
			return character == '"' || character == '(' || character == '[' || character == '<';
		}

		bool LooksLikePath(std::string_view run)
		{
			while (!run.empty() && IsOpeningPunctuation(run.front()))
			{
				run.remove_prefix(1);
			}

			const bool hasSeparator = run.find_first_of("/\\") != std::string_view::npos;

			if (!hasSeparator || !ContainsWordCharacter(run))
			{
				return false;
			}

			const bool startsWithDrive = run.size() >= kDrivePrefixLength &&
										 std::isalpha(static_cast<unsigned char>(run[0])) != 0 && run[1] == ':' &&
										 IsPathSeparator(run[2]);
			const bool startsAtRoot = IsPathSeparator(run[0]);

			if (startsWithDrive || startsAtRoot)
			{
				return true;
			}

			for (const std::string_view prefix : kRelativePrefixes)
			{
				if (run.starts_with(prefix))
				{
					return true;
				}
			}

			const bool startsWithHiddenName =
				run.size() >= kDotAndOneCharacterLength && run[0] == '.' && IsWordCharacter(run[1]);

			return startsWithHiddenName;
		}

		struct Piece
		{
			std::string_view Text;
			ToastTextStyle Style = ToastTextStyle::Plain;
			bool BreaksAsPath = false;
		};

		struct GluedPieces
		{
			std::vector<Piece> Pieces;
			int NewlinesBefore = 0;
		};

		std::size_t PlainRunEnd(std::string_view text, std::size_t runStart)
		{
			std::size_t runEnd = runStart + 1;

			while (runEnd < text.size() && !IsAsciiWhitespace(text[runEnd]) &&
				   !ClosingQuoteOfStrongSpan(text, runEnd).has_value())
			{
				++runEnd;
			}

			return runEnd;
		}

		std::vector<GluedPieces> Tokenise(std::string_view text)
		{
			std::vector<GluedPieces> groups;
			int newlinesSinceLastGroup = 0;
			bool gluedToPreviousPiece = false;
			std::size_t position = 0;

			while (position < text.size())
			{
				const char character = text[position];

				if (character == kNewline)
				{
					++newlinesSinceLastGroup;
					gluedToPreviousPiece = false;
					++position;
					continue;
				}

				if (IsSpaceWithinLine(character))
				{
					gluedToPreviousPiece = false;
					++position;
					continue;
				}

				Piece piece;

				if (const std::optional<std::size_t> closingQuote = ClosingQuoteOfStrongSpan(text, position);
					closingQuote.has_value())
				{
					const std::size_t spanLength = *closingQuote - position + 1;
					const std::size_t innerLength = *closingQuote - position - 1;
					const std::string_view inner = text.substr(position + 1, innerLength);

					piece = Piece{.Text = text.substr(position, spanLength),
								  .Style = ToastTextStyle::Strong,
								  .BreaksAsPath = LooksLikePath(inner)};
					position = *closingQuote + 1;
				}
				else
				{
					const std::size_t runEnd = PlainRunEnd(text, position);
					const std::string_view run = text.substr(position, runEnd - position);
					const bool startsAsPath = !gluedToPreviousPiece && LooksLikePath(run);

					piece = Piece{.Text = run,
								  .Style = startsAsPath ? ToastTextStyle::Path : ToastTextStyle::Plain,
								  .BreaksAsPath = startsAsPath};
					position = runEnd;
				}

				if (gluedToPreviousPiece && !groups.empty())
				{
					groups.back().Pieces.push_back(piece);
				}
				else
				{
					groups.push_back(GluedPieces{.Pieces = {piece}, .NewlinesBefore = newlinesSinceLastGroup});
					newlinesSinceLastGroup = 0;
				}

				gluedToPreviousPiece = true;
			}

			return groups;
		}

		class Layout
		{
		public:
			Layout(float lineWidth, const ToastTextMeasure& measure)
				: m_LineWidth(lineWidth), m_Measure(measure), m_SpaceWidth(measure(" ", kRegularFace))
			{
			}

			void Add(const GluedPieces& glued, bool isFirstGroup)
			{
				if (isFirstGroup)
				{
					NewLine();
				}
				else
				{
					for (int newlineIndex = 0; newlineIndex < glued.NewlinesBefore; ++newlineIndex)
					{
						NewLine();
					}
				}

				if (glued.Pieces.front().BreaksAsPath && LineHasContent())
				{
					NewLine();
				}

				float groupWidth = 0.0f;

				for (const Piece& piece : glued.Pieces)
				{
					groupWidth += Measure(piece);
				}

				float gap = LineHasContent() ? m_SpaceWidth : kNoGap;

				if (LineHasContent() && m_CursorX + gap + groupWidth > m_LineWidth)
				{
					NewLine();
					gap = kNoGap;
				}

				for (const Piece& piece : glued.Pieces)
				{
					Place(piece, gap);
					gap = kNoGap;
				}
			}

			std::vector<ToastTextLine> Take() { return std::move(m_Lines); }

		private:
			float m_LineWidth;
			const ToastTextMeasure& m_Measure;
			float m_SpaceWidth;

			std::vector<ToastTextLine> m_Lines;
			float m_CursorX = 0.0f;

			static bool Bold(ToastTextStyle style) { return style == ToastTextStyle::Strong; }

			bool LineHasContent() const { return m_CursorX > 0.0f; }

			float Measure(const Piece& piece) const { return m_Measure(piece.Text, Bold(piece.Style)); }

			void NewLine()
			{
				m_Lines.emplace_back();
				m_CursorX = 0.0f;
			}

			// WARNING: only plain words merge into one run, because a space measured in the regular face between two bold spans would not be the space the bold face draws.
			void Append(std::string_view text, ToastTextStyle style, float width, float gap)
			{
				std::vector<ToastTextRun>& runs = m_Lines.back().Runs;
				bool mergesWithPreviousRun = false;

				if (style == ToastTextStyle::Plain && !runs.empty())
				{
					const ToastTextRun& previousRun = runs.back();
					mergesWithPreviousRun =
						previousRun.Style == ToastTextStyle::Plain && previousRun.X + previousRun.Width == m_CursorX;
				}

				if (mergesWithPreviousRun)
				{
					ToastTextRun& previousRun = runs.back();

					if (gap > 0.0f)
					{
						previousRun.Text += ' ';
					}

					previousRun.Text += text;
					previousRun.Width += gap + width;
				}
				else
				{
					runs.push_back(
						ToastTextRun{.Text = std::string(text), .Style = style, .X = m_CursorX + gap, .Width = width});
				}

				m_CursorX += gap + width;
			}

			// WARNING: binary-searches the boundaries because each probe measures a whole prefix with the real font, and a linear scan would measure one per character.
			std::size_t FittingCharacterPrefixLength(std::string_view text, bool bold, float room) const
			{
				std::vector<std::size_t> boundaries;

				for (std::size_t offset = 1; offset <= text.size(); ++offset)
				{
					if (offset == text.size() || !IsUtf8ContinuationByte(text[offset]))
					{
						boundaries.push_back(offset);
					}
				}

				std::size_t fittingCount = 0;
				std::size_t searchEnd = boundaries.size();

				while (fittingCount < searchEnd)
				{
					const std::size_t probe = std::midpoint(fittingCount, searchEnd);

					if (m_Measure(text.substr(0, boundaries[probe]), bold) <= room)
					{
						fittingCount = probe + 1;
					}
					else
					{
						searchEnd = probe;
					}
				}

				return fittingCount == 0 ? 0 : boundaries[fittingCount - 1];
			}

			std::size_t FittingPathPrefixLength(std::string_view text, bool bold, float room) const
			{
				std::size_t afterLastFittingSeparator = 0;

				for (std::size_t offset = 0; offset < text.size(); ++offset)
				{
					if (IsPathSeparator(text[offset]))
					{
						if (m_Measure(text.substr(0, offset + 1), bold) > room)
						{
							break;
						}

						afterLastFittingSeparator = offset + 1;
					}
				}

				std::size_t segmentEnd = text.find_first_of("/\\", afterLastFittingSeparator);
				segmentEnd = segmentEnd == std::string_view::npos ? text.size() : segmentEnd + 1;

				const std::string_view nextSegment =
					text.substr(afterLastFittingSeparator, segmentEnd - afterLastFittingSeparator);
				const bool segmentWiderThanWholeLine = m_Measure(nextSegment, bold) > m_LineWidth;

				if (segmentWiderThanWholeLine)
				{
					return FittingCharacterPrefixLength(text, bold, room);
				}

				return afterLastFittingSeparator;
			}

			void Place(const Piece& piece, float gap)
			{
				std::string_view text = piece.Text;
				const bool bold = Bold(piece.Style);

				while (true)
				{
					const float width = m_Measure(text, bold);

					if (m_CursorX + gap + width <= m_LineWidth)
					{
						Append(text, piece.Style, width, gap);
						return;
					}

					const float room = m_LineWidth - m_CursorX - gap;
					std::size_t fitLength = piece.BreaksAsPath ? FittingPathPrefixLength(text, bold, room)
															   : FittingCharacterPrefixLength(text, bold, room);

					if (fitLength == 0)
					{
						if (LineHasContent())
						{
							NewLine();
							gap = kNoGap;
							continue;
						}

						fitLength = CharacterBoundaryAtOrAfter(text, kAtLeastOneByte);
					}

					const std::string_view head = text.substr(0, fitLength);
					Append(head, piece.Style, m_Measure(head, bold), gap);
					text.remove_prefix(fitLength);

					if (text.empty())
					{
						return;
					}

					NewLine();
					gap = kNoGap;
				}
			}
		};
	}

	std::vector<ToastTextLine> LayoutToastText(std::string_view message, float lineWidth, const ToastTextMeasure& measure)
	{
		const std::vector<GluedPieces> groups = Tokenise(message);

		if (groups.empty())
		{
			return {};
		}

		Layout layout(lineWidth, measure);

		for (std::size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
		{
			const bool isFirstGroup = groupIndex == 0;
			layout.Add(groups[groupIndex], isFirstGroup);
		}

		return layout.Take();
	}
}
