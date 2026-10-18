#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	enum class FileTemplate
	{
		Empty,
		CppClass,
		CSharpClass
	};

	inline constexpr std::array<FileTemplate, 3> kFileTemplates{FileTemplate::Empty, FileTemplate::CppClass,
																FileTemplate::CSharpClass};

	struct TemplateFile
	{
		std::string Name;
		std::string Content;
	};

	std::string_view TemplateLabel(FileTemplate fileTemplate);

	// What the name field asks for: a file name for an empty file, a class name for the others.
	std::string_view TemplateNameHint(FileTemplate fileTemplate);

	// Empty when the name is fit for the template; the sentence to show when it is not.
	std::optional<std::string> TemplateNameProblem(FileTemplate fileTemplate, std::string_view name);

	// WARNING: Valid only for a name TemplateNameProblem accepted.
	std::vector<TemplateFile> ExpandTemplate(FileTemplate fileTemplate, std::string_view name, std::string_view csharpNamespace);

	// The namespace a C# file gets: the folders from the workspace root down, each cleaned into an identifier
	// and joined by dots, or the root's own name for a file at the root.
	std::string CSharpNamespaceFor(const std::vector<std::string>& foldersBelowRoot, std::string_view rootName);
}
