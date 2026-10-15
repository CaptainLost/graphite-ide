#include "Language/Server/ClangdHover.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Strings.h"

#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {

		constexpr std::string_view kHeadingPrefix = "### ";
		constexpr std::string_view kCodeFence = "```";
		constexpr std::string_view kRule = "---";
		constexpr std::string_view kScopePrefix = "// In ";
		constexpr std::string_view kCppLanguage = "cpp";
		constexpr std::string_view kParameterBullet = "- ";
		constexpr std::string_view kParametersLine = "Parameters:";
		constexpr std::string_view kReturnTypePrefix = "→ ";
		constexpr std::string_view kTypePrefix = "Type: ";
		constexpr std::string_view kValuePrefix = "Value = ";
		constexpr std::string_view kOffsetPrefix = "Offset: ";
		constexpr std::string_view kSizePrefix = "Size: ";
		constexpr std::string_view kAliasOpening = " (aka ";
		constexpr std::string_view kAlignmentSeparator = ", alignment ";
		constexpr std::string_view kPaddingOpening = " (+";
		constexpr std::string_view kPaddingClosing = " padding)";
		constexpr std::string_view kSpacesAndTabs = " \t";
		constexpr std::string_view kSpaces = " ";
		constexpr std::size_t kAccessSuffixLength = 2;
		constexpr std::size_t kQualifierSeparatorLength = 1;
		constexpr std::size_t kAliasClosingLength = 1;
		constexpr std::size_t kOneBeforeBacktick = 1;

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
			std::string spaced{kind};

			for (char& character : spaced)
			{
				if (character == '-')
				{
					character = ' ';
				}
			}

			return spaced;
		}

		// WARNING: Markdown escapes any ASCII punctuation with a backslash, and clangd does so for a doc line that starts with "-",
		// while the tooltip shows plain text.
		std::string Unescape(std::string_view text)
		{
			std::string unescaped;

			for (std::size_t characterIndex = 0; characterIndex < text.size(); ++characterIndex)
			{
				const bool escapesPunctuation = text[characterIndex] == '\\' && characterIndex + 1 < text.size() &&
												std::ispunct(static_cast<unsigned char>(text[characterIndex + 1]));

				if (escapesPunctuation)
				{
					++characterIndex;
				}

				unescaped += text[characterIndex];
			}

			return unescaped;
		}

		std::string JoinDocumentation(const std::vector<std::string_view>& lines)
		{
			std::string joined;
			bool paragraphBreak = false;

			for (const std::string_view rawLine : lines)
			{
				const std::string_view line = Core::WithoutTrailing(rawLine, kSpaces);

				if (line.empty())
				{
					paragraphBreak = !joined.empty();
					continue;
				}

				if (!joined.empty())
				{
					joined += paragraphBreak ? "\n\n" : "\n";
				}

				paragraphBreak = false;
				joined += Unescape(line);
			}

			return joined;
		}

		void ReadSize(std::string_view text, HoverInfo& info)
		{
			const std::size_t alignmentStart = text.find(kAlignmentSeparator);
			std::string_view size = text.substr(0, alignmentStart);

			if (alignmentStart != std::string_view::npos)
			{
				info.Alignment = std::string(text.substr(alignmentStart + kAlignmentSeparator.size()));
			}

			if (const std::size_t paddingOpening = size.find(kPaddingOpening); paddingOpening != std::string_view::npos)
			{
				std::string_view padding = size.substr(paddingOpening + kPaddingOpening.size());

				const std::size_t paddingClosing = padding.find(kPaddingClosing);

				if (paddingClosing != std::string_view::npos)
				{
					padding = padding.substr(0, paddingClosing);
				}

				info.Padding = std::string(padding);
				size = size.substr(0, paddingOpening);
			}

			info.Size = std::string(size);
		}

		void ReadDeclaration(std::string_view line, HoverInfo& info)
		{
			constexpr std::array<std::string_view, 3> kAccessPrefixes{"public: ", "private: ", "protected: "};

			for (const std::string_view accessPrefix : kAccessPrefixes)
			{
				if (line.starts_with(accessPrefix))
				{
					info.Access = std::string(accessPrefix.substr(0, accessPrefix.size() - kAccessSuffixLength));
					line.remove_prefix(accessPrefix.size());
					break;
				}
			}

			info.Declaration = std::string(line);

			// WARNING: Qualifiers are read off the declaration rather than inferred from the kind, because clangd's kind says
			// "instance-method" for a virtual and a non-virtual method alike.
			constexpr std::array<std::string_view, 3> kQualifierPrefixes{"virtual ", "static ", "const "};

			bool foundQualifier = true;

			while (foundQualifier)
			{
				foundQualifier = false;

				for (const std::string_view qualifierPrefix : kQualifierPrefixes)
				{
					if (line.starts_with(qualifierPrefix))
					{
						info.Qualifiers.emplace_back(qualifierPrefix.substr(0, qualifierPrefix.size() - kQualifierSeparatorLength));
						line.remove_prefix(qualifierPrefix.size());
						foundQualifier = true;
					}
				}
			}
		}
	}

	HoverInfo ParseClangdHover(std::string_view markdown)
	{
		HoverInfo info;
		info.Raw = std::string(markdown);

		const std::vector<std::string_view> lines = Core::SplitLines(markdown);

		if (lines.empty() || !lines[0].starts_with(kHeadingPrefix))
		{
			return info;
		}

		info.Parsed = true;

		std::string_view heading = lines[0].substr(kHeadingPrefix.size());

		if (const std::size_t backtick = heading.find('`'); backtick != std::string_view::npos)
		{
			const std::size_t kindLength = backtick == 0 ? 0 : backtick - kOneBeforeBacktick;
			info.Kind = WithSpaces(heading.substr(0, kindLength));
			info.Name = std::string(Unquote(heading.substr(backtick)));
		}

		// WARNING: Only the start of the header section is structured. From its first free-text line on, a line that looks like
		// "Type: ..." is prose.
		int ruleCount = 0;
		bool insideCodeBlock = false;
		bool insideFields = true;
		bool insideParameters = false;
		std::string codeLanguage;
		std::string declaration;
		std::vector<std::string_view> documentation;

		for (std::size_t lineIndex = 1; lineIndex < lines.size(); ++lineIndex)
		{
			const std::string_view line = lines[lineIndex];

			if (line.starts_with(kCodeFence))
			{
				insideCodeBlock = !insideCodeBlock;
				codeLanguage = insideCodeBlock ? std::string(line.substr(kCodeFence.size())) : std::string();

				if (!insideCodeBlock && codeLanguage.empty())
				{
					documentation.emplace_back();
				}

				continue;
			}

			if (insideCodeBlock)
			{
				if (codeLanguage != kCppLanguage)
				{
					documentation.push_back(line);
				}
				else if (line.starts_with(kScopePrefix))
				{
					info.Scope = std::string(line.substr(kScopePrefix.size()));
				}
				else
				{
					const std::string_view trimmed = Core::WithoutSurrounding(line, kSpacesAndTabs);

					if (!declaration.empty() && !trimmed.empty())
					{
						declaration += ' ';
					}

					declaration += trimmed;
				}

				continue;
			}

			if (line == kRule)
			{
				++ruleCount;
				insideFields = false;
				insideParameters = false;

				const bool firstRule = ruleCount == 1;

				if (firstRule)
				{
					insideFields = true;
				}

				continue;
			}

			const bool insideHeaderSection = ruleCount == 1;

			if (!insideHeaderSection)
			{
				documentation.push_back(line);
				continue;
			}

			if (insideFields)
			{
				if (line.empty())
				{
					continue;
				}

				if (insideParameters)
				{
					if (line.starts_with(kParameterBullet))
					{
						info.Parameters.emplace_back(Unquote(line.substr(kParameterBullet.size())));
						continue;
					}

					insideParameters = false;
				}

				if (line.starts_with(kReturnTypePrefix))
				{
					info.ReturnType = std::string(Unquote(line.substr(kReturnTypePrefix.size())));
					continue;
				}

				if (line == kParametersLine)
				{
					insideParameters = true;
					continue;
				}

				if (line.starts_with(kTypePrefix))
				{
					std::string_view type = Unquote(line.substr(kTypePrefix.size()));

					const std::size_t aliasOpening = type.find(kAliasOpening);

					if (aliasOpening != std::string_view::npos && type.back() == ')')
					{
						const std::size_t aliasStart = aliasOpening + kAliasOpening.size();
						const std::size_t aliasLength = type.size() - aliasStart - kAliasClosingLength;
						info.TypeAka = std::string(type.substr(aliasStart, aliasLength));
						type = type.substr(0, aliasOpening);
					}

					info.Type = std::string(type);
					continue;
				}

				if (line.starts_with(kValuePrefix))
				{
					info.Value = std::string(Unquote(line.substr(kValuePrefix.size())));
					continue;
				}

				if (line.starts_with(kOffsetPrefix))
				{
					info.Offset = std::string(line.substr(kOffsetPrefix.size()));
					continue;
				}

				if (line.starts_with(kSizePrefix))
				{
					ReadSize(line.substr(kSizePrefix.size()), info);
					continue;
				}

				insideFields = false;
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
