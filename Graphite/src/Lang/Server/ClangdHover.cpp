#include "Lang/Server/ClangdHover.h"

#include <array>
#include <sstream>

namespace Graphite {

	namespace {
		std::vector<std::string_view> SplitLines(std::string_view text)
		{
			std::vector<std::string_view> lines;
			std::size_t start = 0;

			while (start <= text.size())
			{
				const std::size_t end = text.find('\n', start);
				std::string_view line = text.substr(start, end == std::string_view::npos ? text.size() - start : end - start);

				if (!line.empty() && line.back() == '\r')
					line.remove_suffix(1);

				lines.push_back(line);

				if (end == std::string_view::npos)
					break;

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
				return text.substr(1, text.size() - 2);

			return text;
		}

		std::string WithSpaces(std::string_view kind)
		{
			std::string result{kind};

			for (char& c : result)
			{
				if (c == '-')
					c = ' ';
			}

			return result;
		}

		void AppendParagraph(std::string& into, std::string_view text)
		{
			if (text.empty())
				return;

			if (!into.empty())
				into += "\n\n";

			into += text;
		}

		// "1 byte (+7 bytes padding), alignment 1 byte"
		void ReadSize(std::string_view text, HoverInfo& info)
		{
			const std::size_t comma = text.find(", alignment ");
			std::string_view size = text.substr(0, comma);

			if (comma != std::string_view::npos)
				info.Alignment = std::string(text.substr(comma + std::string_view(", alignment ").size()));

			if (const std::size_t paren = size.find(" (+"); paren != std::string_view::npos)
			{
				std::string_view padding = size.substr(paren + 3);

				if (const std::size_t word = padding.find(" padding)"); word != std::string_view::npos)
					padding = padding.substr(0, word);

				info.Padding = std::string(padding);
				size = size.substr(0, paren);
			}

			info.Size = std::string(size);
		}

		// The code block's declaration: "public: virtual void OnUpdate(Timestep ts)".
		void ReadDeclaration(std::string_view line, HoverInfo& info)
		{
			constexpr std::array<std::string_view, 3> kAccess{"public: ", "private: ", "protected: "};

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
			return info;

		info.Parsed = true;

		// "### instance-method `OnUpdate`" or, for an include, "### `vector`".
		std::string_view heading = lines[0].substr(4);

		if (const std::size_t tick = heading.find('`'); tick != std::string_view::npos)
		{
			info.Kind = WithSpaces(heading.substr(0, tick == 0 ? 0 : tick - 1));
			info.Name = std::string(Unquote(heading.substr(tick)));
		}

		bool inParameters = false;
		bool inCode = false;
		std::string codeLanguage;

		for (std::size_t index = 1; index < lines.size(); ++index)
		{
			const std::string_view line = lines[index];

			if (StartsWith(line, "```"))
			{
				inCode = !inCode;
				codeLanguage = inCode ? std::string(line.substr(3)) : std::string();
				continue;
			}

			if (inCode)
			{
				if (codeLanguage == "cpp" && StartsWith(line, "// In "))
					info.Scope = std::string(line.substr(6));
				else if (codeLanguage == "cpp")
					ReadDeclaration(line, info);
				else
					AppendParagraph(info.Documentation, line);

				continue;
			}

			if (line == "---" || line.empty())
			{
				inParameters = inParameters && line.empty();
				continue;
			}

			if (StartsWith(line, "→ "))
			{
				info.ReturnType = std::string(Unquote(line.substr(std::string_view("→ ").size())));
				continue;
			}

			if (line == "Parameters:")
			{
				inParameters = true;
				continue;
			}

			if (inParameters && StartsWith(line, "- "))
			{
				info.Parameters.emplace_back(Unquote(line.substr(2)));
				continue;
			}

			inParameters = false;

			if (StartsWith(line, "Type: "))
			{
				std::string_view type = Unquote(line.substr(6));

				if (const std::size_t aka = type.find(" (aka "); aka != std::string_view::npos && type.back() == ')')
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

			AppendParagraph(info.Documentation, line);
		}

		return info;
	}
}
