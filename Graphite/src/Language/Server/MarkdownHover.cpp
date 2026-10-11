#include "Language/Server/MarkdownHover.h"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		constexpr std::string_view kFence = "```";
		constexpr std::string_view kBlanks = " \t";
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
			const std::size_t last = text.find_last_not_of(kBlanks);
			return last == std::string_view::npos ? std::string_view{} : text.substr(0, last + 1);
		}

		std::string_view Trimmed(std::string_view text)
		{
			const std::size_t first = text.find_first_not_of(kBlanks);
			return first == std::string_view::npos ? std::string_view{} : TrimmedRight(text.substr(first));
		}

		std::vector<std::string_view> LinesOf(std::string_view text)
		{
			std::vector<std::string_view> lines;
			std::size_t start = 0;

			while (start <= text.size())
			{
				const std::size_t end = text.find('\n', start);
				std::string_view line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);

				if (line.ends_with('\r'))
				{
					line.remove_suffix(1);
				}

				lines.push_back(line);

				if (end == std::string_view::npos)
				{
					break;
				}

				start = end + 1;
			}

			return lines;
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
			std::size_t at = 0;

			while (at < line.size())
			{
				const char character = line[at];
				const bool escapes = character == kBackslash && at + 1 < line.size() && IsAsciiPunctuation(line[at + 1]);

				if (escapes)
				{
					text.push_back(line[at + 1]);
					at += 2;
					continue;
				}

				if (character == kBacktick)
				{
					++at;
					continue;
				}

				if (const Entity* entity = character == kAmpersand ? EntityAt(line.substr(at)) : nullptr)
				{
					text += entity->Text;
					at += entity->Name.size();
					continue;
				}

				text.push_back(character);
				++at;
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

		// Blank lines at either end of a block say nothing.
		std::string DeclarationFrom(std::vector<std::string> lines)
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
			return JoinedLines(lines);
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

		for (const std::string_view line : LinesOf(markdown))
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

		info.Declaration = DeclarationFrom(std::move(declaration));
		info.Documentation = documentation.Text();
		info.Parsed = !info.Declaration.empty() || !info.Documentation.empty();

		return info;
	}
}
