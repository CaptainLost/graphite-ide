#include "Lang/Server/ClangdHover.h"

#include <array>
#include <cctype>
#include <string>

namespace Graphite {

	namespace {
		std::vector<std::string_view> SplitLines(std::string_view text)
		{
			std::vector<std::string_view> lines;
			std::size_t start = 0;

			while (start <= text.size())
			{
				const std::size_t end = text.find('\n', start);
				const std::size_t length =
					end == std::string_view::npos ? text.size() - start : end - start;
				std::string_view line = text.substr(start, length);

				if (!line.empty() && line.back() == '\r')
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

		bool StartsWith(std::string_view text, std::string_view prefix)
		{
			return text.substr(0, prefix.size()) == prefix;
		}

		// "`text`" -> "text"; anything not wrapped is returned as it is.
		std::string_view Unquote(std::string_view text)
		{
			if (text.size() >= 2 && text.front() == '`' && text.back() == '`')
			{
				return text.substr(1, text.size() - 2);
			}

			return text;
		}

		std::string WithSpaces(std::string_view kind)
		{
			std::string result{kind};

			for (char& c : result)
			{
				if (c == '-')
				{
					c = ' ';
				}
			}

			return result;
		}

		// Markdown escapes any ASCII punctuation with a backslash; clangd does so for a doc line
		// that starts with "-" and for the like, and the tooltip shows plain text.
		std::string Unescape(std::string_view text)
		{
			std::string result;

			for (std::size_t i = 0; i < text.size(); ++i)
			{
				if (text[i] == '\\' && i + 1 < text.size() &&
					std::ispunct(static_cast<unsigned char>(text[i + 1])))
				{
					++i;
				}

				result += text[i];
			}

			return result;
		}

		// Consecutive lines are one paragraph, as in markdown; a blank line starts the next. The
		// two trailing spaces clangd writes for a hard break are dropped, the newline they stand
		// for stays.
		std::string JoinDocumentation(const std::vector<std::string_view>& lines)
		{
			std::string result;
			bool paragraphBreak = false;

			for (std::string_view line : lines)
			{
				while (!line.empty() && line.back() == ' ')
				{
					line.remove_suffix(1);
				}

				if (line.empty())
				{
					paragraphBreak = !result.empty();
					continue;
				}

				if (!result.empty())
				{
					result += paragraphBreak ? "\n\n" : "\n";
				}

				paragraphBreak = false;
				result += Unescape(line);
			}

			return result;
		}

		// "1 byte (+7 bytes padding), alignment 1 byte"
		void ReadSize(std::string_view text, HoverInfo& info)
		{
			constexpr std::string_view kAlignment = ", alignment ";
			const std::size_t comma = text.find(kAlignment);
			std::string_view size = text.substr(0, comma);

			if (comma != std::string_view::npos)
			{
				info.Alignment = std::string(text.substr(comma + kAlignment.size()));
			}

			if (const std::size_t paren = size.find(" (+"); paren != std::string_view::npos)
			{
				std::string_view padding = size.substr(paren + 3);

				const std::size_t word = padding.find(" padding)");

				if (word != std::string_view::npos)
				{
					padding = padding.substr(0, word);
				}

				info.Padding = std::string(padding);
				size = size.substr(0, paren);
			}

			info.Size = std::string(size);
		}

		// The code block's declaration: "public: virtual void OnUpdate(Timestep ts)".
		void ReadDeclaration(std::string_view line, HoverInfo& info)
		{
			constexpr std::array<std::string_view, 3> kAccess{"public: ", "private: ",
															  "protected: "};

			for (std::string_view access : kAccess)
			{
				if (StartsWith(line, access))
				{
					info.Access = std::string(access.substr(0, access.size() - 2));
					line.remove_prefix(access.size());
					break;
				}
			}

			info.Declaration = std::string(line);

			// Qualifiers are read off the declaration rather than inferred from the kind: clangd's
			// kind says "instance-method" for a virtual and a non-virtual method alike.
			constexpr std::array<std::string_view, 3> kQualifiers{"virtual ", "static ", "const "};

			for (bool found = true; found;)
			{
				found = false;

				for (std::string_view qualifier : kQualifiers)
				{
					if (StartsWith(line, qualifier))
					{
						info.Qualifiers.emplace_back(qualifier.substr(0, qualifier.size() - 1));
						line.remove_prefix(qualifier.size());
						found = true;
					}
				}
			}
		}
	}

	HoverInfo ParseClangdHover(std::string_view markdown)
	{
		HoverInfo info;
		info.Raw = std::string(markdown);

		const std::vector<std::string_view> lines = SplitLines(markdown);

		if (lines.empty() || !StartsWith(lines[0], "### "))
		{
			return info;
		}

		info.Parsed = true;

		// "### instance-method `OnUpdate`" or, for an include, "### `vector`".
		std::string_view heading = lines[0].substr(4);

		if (const std::size_t tick = heading.find('`'); tick != std::string_view::npos)
		{
			info.Kind = WithSpaces(heading.substr(0, tick == 0 ? 0 : tick - 1));
			info.Name = std::string(Unquote(heading.substr(tick)));
		}

		// The reply is split by "---" rules: the heading, then the header section with the
		// structured fields, then the code block and whatever follows it. Only the start of the
		// header section is structured; from its first free-text line on, a line that looks like
		// "Type: ..." is prose.
		int rules = 0;
		bool inCode = false;
		bool inFields = true;
		bool inParameters = false;
		std::string codeLanguage;
		std::string declaration;
		std::vector<std::string_view> documentation;

		for (std::size_t index = 1; index < lines.size(); ++index)
		{
			const std::string_view line = lines[index];

			if (StartsWith(line, "```"))
			{
				inCode = !inCode;
				codeLanguage = inCode ? std::string(line.substr(3)) : std::string();

				if (!inCode && codeLanguage.empty())
				{
					documentation.emplace_back();
				}

				continue;
			}

			if (inCode)
			{
				if (codeLanguage != "cpp")
				{
					documentation.push_back(line);
				}
				else if (StartsWith(line, "// In "))
				{
					info.Scope = std::string(line.substr(6));
				}
				else
				{
					// A declaration clangd wrapped over several lines is one declaration.
					std::string_view trimmed = line;

					while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
					{
						trimmed.remove_prefix(1);
					}

					while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
					{
						trimmed.remove_suffix(1);
					}

					if (!declaration.empty() && !trimmed.empty())
					{
						declaration += ' ';
					}

					declaration += trimmed;
				}

				continue;
			}

			if (line == "---")
			{
				++rules;
				inFields = false;
				inParameters = false;

				// Fields can only start right after the first rule.
				if (rules == 1)
				{
					inFields = true;
				}

				continue;
			}

			if (rules != 1)
			{
				documentation.push_back(line);
				continue;
			}

			if (inFields)
			{
				if (line.empty())
				{
					continue;
				}

				if (inParameters)
				{
					if (StartsWith(line, "- "))
					{
						info.Parameters.emplace_back(Unquote(line.substr(2)));
						continue;
					}

					inParameters = false;
				}

				constexpr std::string_view kArrow = "→ ";

				if (StartsWith(line, kArrow))
				{
					info.ReturnType = std::string(Unquote(line.substr(kArrow.size())));
					continue;
				}

				if (line == "Parameters:")
				{
					inParameters = true;
					continue;
				}

				if (StartsWith(line, "Type: "))
				{
					std::string_view type = Unquote(line.substr(6));

					const std::size_t aka = type.find(" (aka ");

					if (aka != std::string_view::npos && type.back() == ')')
					{
						info.TypeAka = std::string(type.substr(aka + 6, type.size() - aka - 7));
						type = type.substr(0, aka);
					}

					info.Type = std::string(type);
					continue;
				}

				if (StartsWith(line, "Value = "))
				{
					info.Value = std::string(Unquote(line.substr(8)));
					continue;
				}

				if (StartsWith(line, "Offset: "))
				{
					info.Offset = std::string(line.substr(8));
					continue;
				}

				if (StartsWith(line, "Size: "))
				{
					ReadSize(line.substr(6), info);
					continue;
				}

				inFields = false;
			}

			documentation.push_back(line);
		}

		if (!declaration.empty())
		{
			ReadDeclaration(declaration, info);
		}

		info.Documentation = JoinDocumentation(documentation);

		return info;
	}
}
