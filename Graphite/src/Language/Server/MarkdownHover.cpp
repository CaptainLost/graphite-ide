#include "Language/Server/MarkdownHover.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Strings.h"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		constexpr std::string_view kFence = "```";
		constexpr std::string_view kBlanks = " \t";
		constexpr std::string_view kOverloadNoteOpening = "(+";
		constexpr char kBackslash = '\\';
		constexpr char kBacktick = '`';
		constexpr char kAmpersand = '&';

		struct Entity
		{
			std::string_view Name;
			std::string_view Text;
		};

		constexpr std::array<Entity, 6> kEntities{{
			{.Name = "&nbsp;", .Text = " "},
			{.Name = "&lt;", .Text = "<"},
			{.Name = "&gt;", .Text = ">"},
			{.Name = "&quot;", .Text = "\""},
			{.Name = "&#39;", .Text = "'"},
			{.Name = "&amp;", .Text = "&"},
		}};

		std::string_view TrimmedRight(std::string_view text)
		{
			return Core::WithoutTrailing(text, kBlanks);
		}

		std::string_view Trimmed(std::string_view text)
		{
			return Core::WithoutSurrounding(text, kBlanks);
		}

		bool IsFence(std::string_view line)
		{
			return Trimmed(line).starts_with(kFence);
		}

		bool IsAsciiPunctuation(char character)
		{
			return (character >= '!' && character <= '/') || (character >= ':' && character <= '@') ||
				   (character >= '[' && character <= '`') || (character >= '{' && character <= '~');
		}

		const Entity* EntityAt(std::string_view text)
		{
			for (const Entity& entity : kEntities)
			{
				if (text.starts_with(entity.Name))
				{
					return &entity;
				}
			}

			return nullptr;
		}

		std::string PlainTextOf(std::string_view line)
		{
			std::string text;
			std::size_t characterIndex = 0;

			while (characterIndex < line.size())
			{
				const char character = line[characterIndex];
				const bool escapes = character == kBackslash && characterIndex + 1 < line.size() && IsAsciiPunctuation(line[characterIndex + 1]);

				if (escapes)
				{
					text.push_back(line[characterIndex + 1]);
					characterIndex += 2;
					continue;
				}

				if (character == kBacktick)
				{
					++characterIndex;
					continue;
				}

				if (const Entity* entity = character == kAmpersand ? EntityAt(line.substr(characterIndex)) : nullptr)
				{
					text += entity->Text;
					characterIndex += entity->Name.size();
					continue;
				}

				text.push_back(character);
				++characterIndex;
			}

			return std::string{Trimmed(text)};
		}

		std::string JoinedLines(const std::vector<std::string>& lines)
		{
			std::string joined;

			for (const std::string& line : lines)
			{
				if (!joined.empty())
				{
					joined.push_back('\n');
				}

				joined += line;
			}

			return joined;
		}

		std::vector<std::string> WithoutBlankEnds(std::vector<std::string> lines)
		{
			while (!lines.empty() && lines.back().empty())
			{
				lines.pop_back();
			}

			std::size_t firstWritten = 0;

			while (firstWritten < lines.size() && lines[firstWritten].empty())
			{
				++firstWritten;
			}

			lines.erase(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(firstWritten));
			return lines;
		}

		bool IsDigit(char character)
		{
			return character >= '0' && character <= '9';
		}

		std::size_t OverloadNoteStart(std::string_view line)
		{
			const std::size_t noteStart = line.rfind(kOverloadNoteOpening);

			if (noteStart == std::string_view::npos || !line.ends_with(')'))
			{
				return std::string_view::npos;
			}

			const std::string_view before = line.substr(0, noteStart);
			const std::string_view inside = line.substr(noteStart + kOverloadNoteOpening.size(),
														line.size() - 1 - noteStart - kOverloadNoteOpening.size());
			const std::size_t countStart = inside.find_first_not_of(kBlanks);

			const bool followsTheDeclaration = before.ends_with(' ') && !Trimmed(before).empty();
			const bool opensWithACount = countStart != std::string_view::npos && IsDigit(inside[countStart]);
			const bool closesOnlyAtTheEnd = inside.find_first_of("()") == std::string_view::npos;

			return followsTheDeclaration && opensWithACount && closesOnlyAtTheEnd ? noteStart : std::string_view::npos;
		}

		std::string TakeOverloadNote(std::string& line)
		{
			const std::size_t noteStart = OverloadNoteStart(line);

			if (noteStart == std::string_view::npos)
			{
				return {};
			}

			std::string note = line.substr(noteStart);
			line = std::string{TrimmedRight(std::string_view(line).substr(0, noteStart))};
			return note;
		}

		class Paragraphs
		{
		public:
			void Add(std::string line)
			{
				if (line.empty())
				{
					Close();
					return;
				}

				m_Open.push_back(std::move(line));
			}

			std::string Text()
			{
				Close();

				std::string text;

				for (const std::string& paragraph : m_Closed)
				{
					if (!text.empty())
					{
						text += "\n\n";
					}

					text += paragraph;
				}

				return text;
			}

		private:
			void Close()
			{
				if (!m_Open.empty())
				{
					m_Closed.push_back(JoinedLines(m_Open));
					m_Open.clear();
				}
			}

			std::vector<std::string> m_Open;
			std::vector<std::string> m_Closed;
		};
	}

	HoverInfo ParseMarkdownHover(std::string_view markdown)
	{
		HoverInfo info;
		info.Raw = std::string{markdown};

		std::vector<std::string> declaration;
		Paragraphs documentation;
		bool insideFence = false;
		bool declarationTaken = false;
		bool readingDeclaration = false;

		for (const std::string_view line : Core::SplitLines(markdown))
		{
			if (IsFence(line))
			{
				insideFence = !insideFence;
				readingDeclaration = insideFence && !declarationTaken;
				declarationTaken = true;
				continue;
			}

			if (readingDeclaration)
			{
				declaration.push_back(std::string{TrimmedRight(line)});
				continue;
			}

			documentation.Add(insideFence ? std::string{Trimmed(line)} : PlainTextOf(line));
		}

		std::vector<std::string> declarationLines = WithoutBlankEnds(std::move(declaration));

		if (!declarationLines.empty())
		{
			info.OverloadNote = TakeOverloadNote(declarationLines.back());
		}

		info.Declaration = JoinedLines(declarationLines);
		info.Documentation = documentation.Text();
		info.Parsed = !info.Declaration.empty() || !info.Documentation.empty();

		return info;
	}
}
