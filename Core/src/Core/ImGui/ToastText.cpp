#include "corepch.h"

#include "Core/ImGui/ToastText.h"

#include <cctype>
#include <cstddef>
#include <optional>

namespace Core {

	namespace {
		// Whitespace other than a newline, which ends a line rather than separating words.
		bool IsSpace(char c)
		{
			return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
		}

		bool IsWhitespace(char c)
		{
			return IsSpace(c) || c == '\n';
		}

		// Any byte of a multi-byte UTF-8 character counts: a letter outside ASCII is a letter.
		bool IsWordChar(char c)
		{
			const auto byte = static_cast<unsigned char>(c);
			return byte >= 0x80 || std::isalnum(byte) != 0 || c == '_';
		}

		bool IsSeparator(char c)
		{
			return c == '/' || c == '\\';
		}

		bool IsContinuationByte(char c)
		{
			return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
		}

		// Where the strong span opened by a quote at `open` closes, if that quote opens one. An
		// apostrophe inside a word - "don't" - opens nothing, and one only closes a span where
		// a word ends, so "'it's'" is one span.
		std::optional<std::size_t> StrongEnd(std::string_view text, std::size_t open)
		{
			if (text[open] != '\'' || (open > 0 && IsWordChar(text[open - 1])))
			{
				return std::nullopt;
			}

			for (std::size_t i = open + 2; i < text.size(); ++i)
			{
				if (text[i] == '\n')
				{
					return std::nullopt;
				}

				if (text[i] == '\'' && (i + 1 == text.size() || !IsWordChar(text[i + 1])))
				{
					return i;
				}
			}

			return std::nullopt;
		}

		// A run with a separator in it that starts the way only a path does: a drive, the
		// root, the current or parent folder, home, or a hidden folder such as ".claude/".
		// "src/a.cpp" or "and/or" stay words.
		bool IsPath(std::string_view run)
		{
			if (run.find_first_of("/\\") == std::string_view::npos)
			{
				return false;
			}

			if (run.size() >= 3 && std::isalpha(static_cast<unsigned char>(run[0])) != 0 && run[1] == ':' &&
				IsSeparator(run[2]))
			{
				return true;
			}

			if (IsSeparator(run[0]))
			{
				return true;
			}

			for (const std::string_view prefix : {"./", ".\\", "../", "..\\", "~/", "~\\"})
			{
				if (run.starts_with(prefix))
				{
					return true;
				}
			}

			return run.size() >= 2 && run[0] == '.' && IsWordChar(run[1]);
		}

		struct Piece
		{
			std::string_view Text;
			ToastTextStyle Style = ToastTextStyle::Plain;
		};

		// Pieces with no whitespace between them - a quoted span and the colon after it - which
		// only break apart when together they are wider than a line.
		struct Group
		{
			std::vector<Piece> Pieces;

			// How many newlines came between this group and the one before.
			int Breaks = 0;
		};

		std::vector<Group> Tokenise(std::string_view text)
		{
			std::vector<Group> groups;
			int breaks = 0;
			bool glued = false;
			std::size_t i = 0;

			while (i < text.size())
			{
				const char c = text[i];

				if (c == '\n')
				{
					++breaks;
					glued = false;
					++i;
					continue;
				}

				if (IsSpace(c))
				{
					glued = false;
					++i;
					continue;
				}

				Piece piece;

				if (const std::optional<std::size_t> close = StrongEnd(text, i); close.has_value())
				{
					piece = Piece{text.substr(i, *close - i + 1), ToastTextStyle::Strong};
					i = *close + 1;
				}
				else
				{
					// To the next whitespace, or to a quote that opens a span of its own.
					std::size_t end = i + 1;

					while (end < text.size() && !IsWhitespace(text[end]) && !StrongEnd(text, end).has_value())
					{
						++end;
					}

					const std::string_view run = text.substr(i, end - i);

					// Only a run that starts a word is a path: one glued to a quoted span is the
					// span's tail.
					piece = Piece{run, !glued && IsPath(run) ? ToastTextStyle::Path : ToastTextStyle::Plain};
					i = end;
				}

				if (glued && !groups.empty())
				{
					groups.back().Pieces.push_back(piece);
				}
				else
				{
					groups.push_back(Group{{piece}, breaks});
					breaks = 0;
				}

				glued = true;
			}

			return groups;
		}

		class Layout
		{
		public:
			Layout(float width, const ToastTextMeasure& measure)
				: m_Width(width), m_Measure(measure), m_Space(measure(" ", false))
			{
			}

			void Add(const Group& group, bool first)
			{
				if (first)
				{
					NewLine();
				}
				else
				{
					// The first newline ends the line; each one after leaves an empty line.
					for (int i = 0; i < group.Breaks; ++i)
					{
						NewLine();
					}
				}

				if (group.Pieces.front().Style == ToastTextStyle::Path && m_X > 0.0f)
				{
					NewLine();
				}

				float total = 0.0f;

				for (const Piece& piece : group.Pieces)
				{
					total += Measure(piece);
				}

				float gap = m_X > 0.0f ? m_Space : 0.0f;

				if (m_X > 0.0f && m_X + gap + total > m_Width)
				{
					NewLine();
					gap = 0.0f;
				}

				for (const Piece& piece : group.Pieces)
				{
					Place(piece, gap);
					gap = 0.0f;
				}
			}

			std::vector<ToastTextLine> Take() { return std::move(m_Lines); }

		private:
			float m_Width;
			const ToastTextMeasure& m_Measure;
			float m_Space;

			std::vector<ToastTextLine> m_Lines;
			float m_X = 0.0f;

			static bool Bold(ToastTextStyle style) { return style == ToastTextStyle::Strong; }

			float Measure(const Piece& piece) const { return m_Measure(piece.Text, Bold(piece.Style)); }

			void NewLine()
			{
				m_Lines.emplace_back();
				m_X = 0.0f;
			}

			void Append(std::string_view text, ToastTextStyle style, float width, float gap)
			{
				std::vector<ToastTextRun>& runs = m_Lines.back().Runs;

				// Plain words that follow one another make one run, drawn in one call. Only
				// plain ones: a space measured in the regular face between two bold spans
				// would not be the space the bold face draws.
				if (style == ToastTextStyle::Plain && !runs.empty() && runs.back().Style == ToastTextStyle::Plain &&
					runs.back().X + runs.back().Width == m_X)
				{
					ToastTextRun& last = runs.back();

					if (gap > 0.0f)
					{
						last.Text += ' ';
					}

					last.Text += text;
					last.Width += gap + width;
				}
				else
				{
					runs.push_back(ToastTextRun{std::string(text), style, m_X + gap, width});
				}

				m_X += gap + width;
			}

			// The longest prefix ending on a character boundary no wider than `room`; 0 if
			// not even one character fits. The width only grows with the prefix, so a binary
			// search over the boundaries finds it.
			std::size_t FitCharacters(std::string_view text, bool bold, float room) const
			{
				std::vector<std::size_t> boundaries;

				for (std::size_t i = 1; i <= text.size(); ++i)
				{
					if (i == text.size() || !IsContinuationByte(text[i]))
					{
						boundaries.push_back(i);
					}
				}

				std::size_t low = 0;
				std::size_t high = boundaries.size();

				while (low < high)
				{
					const std::size_t mid = (low + high) / 2;

					if (m_Measure(text.substr(0, boundaries[mid]), bold) <= room)
					{
						low = mid + 1;
					}
					else
					{
						high = mid;
					}
				}

				return low == 0 ? 0 : boundaries[low - 1];
			}

			// The longest prefix of a path that ends just after a separator and fits `room`.
			// When the segment after it is wider than a whole line, it has to break anywhere
			// sooner or later, so it starts doing so here rather than on a line of its own.
			std::size_t FitPath(std::string_view text, float room) const
			{
				std::size_t fit = 0;

				for (std::size_t i = 0; i < text.size(); ++i)
				{
					if (IsSeparator(text[i]))
					{
						if (m_Measure(text.substr(0, i + 1), false) > room)
						{
							break;
						}

						fit = i + 1;
					}
				}

				std::size_t segmentEnd = text.find_first_of("/\\", fit);
				segmentEnd = segmentEnd == std::string_view::npos ? text.size() : segmentEnd + 1;

				if (m_Measure(text.substr(fit, segmentEnd - fit), false) > m_Width)
				{
					return FitCharacters(text, false, room);
				}

				return fit;
			}

			void Place(const Piece& piece, float gap)
			{
				std::string_view text = piece.Text;
				const bool bold = Bold(piece.Style);

				while (true)
				{
					const float width = m_Measure(text, bold);

					if (m_X + gap + width <= m_Width)
					{
						Append(text, piece.Style, width, gap);
						return;
					}

					const float room = m_Width - m_X - gap;
					std::size_t fit = piece.Style == ToastTextStyle::Path ? FitPath(text, room)
																		  : FitCharacters(text, bold, room);

					if (fit == 0)
					{
						if (m_X > 0.0f)
						{
							NewLine();
							gap = 0.0f;
							continue;
						}

						// Not one character fits an empty line: one goes anyway, or the
						// layout would never end.
						fit = 1;

						while (fit < text.size() && IsContinuationByte(text[fit]))
						{
							++fit;
						}
					}

					const std::string_view head = text.substr(0, fit);
					Append(head, piece.Style, m_Measure(head, bold), gap);
					text.remove_prefix(fit);

					if (text.empty())
					{
						return;
					}

					NewLine();
					gap = 0.0f;
				}
			}
		};
	}

	std::vector<ToastTextLine> LayoutToastText(std::string_view message, float width, const ToastTextMeasure& measure)
	{
		const std::vector<Group> groups = Tokenise(message);

		if (groups.empty())
		{
			return {};
		}

		Layout layout(width, measure);

		for (std::size_t i = 0; i < groups.size(); ++i)
		{
			layout.Add(groups[i], i == 0);
		}

		return layout.Take();
	}
}
