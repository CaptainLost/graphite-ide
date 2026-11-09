#include "Explorer/FileTemplates.h"

#include <cctype>
#include <format>

namespace Graphite {

	namespace {
		constexpr std::string_view kFallbackNamespace = "Project";

		bool IsIdentifierStart(char character)
		{
			const unsigned char byte = static_cast<unsigned char>(character);

			return std::isalpha(byte) != 0 || character == '_';
		}

		bool IsIdentifierPart(char character)
		{
			return IsIdentifierStart(character) || std::isdigit(static_cast<unsigned char>(character)) != 0;
		}

		bool IsIdentifier(std::string_view name)
		{
			if (name.empty() || !IsIdentifierStart(name.front()))
			{
				return false;
			}

			for (const char character : name)
			{
				if (!IsIdentifierPart(character))
				{
					return false;
				}
			}

			return true;
		}

		std::string AsIdentifier(std::string_view text)
		{
			std::string identifier;

			for (const char character : text)
			{
				identifier += IsIdentifierPart(character) ? character : '_';
			}

			if (identifier.empty() || !IsIdentifierStart(identifier.front()))
			{
				identifier.insert(identifier.begin(), '_');
			}

			return identifier;
		}
	}

	std::string_view TemplateLabel(FileTemplate fileTemplate)
	{
		switch (fileTemplate)
		{
		case FileTemplate::Empty:
			return "Empty file";
		case FileTemplate::CppClass:
			return "C++ class (.h and .cpp)";
		case FileTemplate::CSharpClass:
			return "C# class (.cs)";
		}

		return {};
	}

	std::string_view TemplateNameHint(FileTemplate fileTemplate)
	{
		return fileTemplate == FileTemplate::Empty ? "File name" : "Class name";
	}

	std::optional<std::string> TemplateNameProblem(FileTemplate fileTemplate, std::string_view name)
	{
		if (name.empty())
		{
			return std::nullopt;
		}

		if (fileTemplate == FileTemplate::Empty)
		{
			return std::nullopt;
		}

		if (!IsIdentifier(name))
		{
			return std::string{"A class name uses letters, digits and underscores, and does not start with a digit."};
		}

		return std::nullopt;
	}

	std::vector<TemplateFile> ExpandTemplate(FileTemplate fileTemplate, std::string_view name, std::string_view csharpNamespace,
											 std::string_view indent)
	{
		switch (fileTemplate)
		{
		case FileTemplate::Empty:
			return {{.Name = std::string{name}, .Content = {}}};

		case FileTemplate::CppClass:
			return {{.Name = std::format("{}.h", name),
					 .Content = std::format("#pragma once\n\nclass {}\n{{\npublic:\n{}{}() = default;\n}};\n", name, indent, name)},
					{.Name = std::format("{}.cpp", name), .Content = std::format("#include \"{}.h\"\n", name)}};

		case FileTemplate::CSharpClass:
			return {{.Name = std::format("{}.cs", name),
					 .Content = std::format("namespace {};\n\npublic class {}\n{{\n}}\n", csharpNamespace, name)}};
		}

		return {};
	}

	std::string CSharpNamespaceFor(const std::vector<std::string>& foldersBelowRoot, std::string_view rootName)
	{
		if (foldersBelowRoot.empty())
		{
			return rootName.empty() ? std::string{kFallbackNamespace} : AsIdentifier(rootName);
		}

		std::string joined;

		for (const std::string& folder : foldersBelowRoot)
		{
			if (!joined.empty())
			{
				joined += '.';
			}

			joined += AsIdentifier(folder);
		}

		return joined;
	}
}
