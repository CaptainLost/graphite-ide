#include "Sln/VcxprojReader.h"

#include "Core/Fs/FileSystem.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr std::string_view kFileItems[] = {"ClCompile", "ClInclude", "None"};

		std::vector<std::string> SplitFilter(std::string_view filter)
		{
			std::vector<std::string> parts;
			std::size_t start = 0;

			while (start < filter.size())
			{
				const std::size_t slash = filter.find('\\', start);
				const std::size_t end = slash == std::string_view::npos ? filter.size() : slash;

				if (end > start)
					parts.emplace_back(filter.substr(start, end - start));

				if (slash == std::string_view::npos)
					break;

				start = slash + 1;
			}

			return parts;
		}

		ContentFolder& EnsureContentFolder(ContentFolder& root,
										   const std::vector<std::string>& parts)
		{
			ContentFolder* current = &root;

			for (const std::string& part : parts)
			{
				const auto match =
					std::find_if(current->Folders.begin(), current->Folders.end(),
								 [&part](const ContentFolder& f) { return f.Name == part; });

				if (match != current->Folders.end())
				{
					current = &*match;
					continue;
				}

				ContentFolder created;
				created.Name = part;
				current->Folders.push_back(std::move(created));
				current = &current->Folders.back();
			}

			return *current;
		}

		std::unordered_map<std::string, std::string> ReadFilterAssignments(
			const std::filesystem::path& filters)
		{
			std::unordered_map<std::string, std::string> assignments;

			std::error_code code;

			if (!std::filesystem::is_regular_file(filters, code))
				return assignments;

			pugi::xml_document document;

			if (!document.load_file(filters.c_str()))
				return assignments;

			for (const pugi::xml_node& group : document.child("Project").children("ItemGroup"))
			{
				for (const pugi::xml_node& item : group.children())
				{
					const pugi::xml_node filter = item.child("Filter");

					if (!filter)
						continue;

					assignments.emplace(item.attribute("Include").value(),
										filter.child_value());
				}
			}

			return assignments;
		}

		void SortContent(ContentFolder& folder)
		{
			std::sort(folder.Folders.begin(), folder.Folders.end(),
					  [](const ContentFolder& left, const ContentFolder& right) { return Core::CompareNames(left.Name, right.Name) < 0; });

			std::sort(folder.Files.begin(), folder.Files.end(),
					  [](const ContentFile& left, const ContentFile& right) { return Core::CompareNames(left.Name, right.Name) < 0; });

			for (ContentFolder& child : folder.Folders)
				SortContent(child);
		}
	}

	bool VcxprojReader::Handles(const std::filesystem::path& project) const
	{
		// Compared case-insensitively, as ReadSolution does for the solution itself: the
		// filesystem does not care, so App.VCXPROJ must not silently render as a leaf.
		std::string extension = Core::ToUtf8(project.extension());
		std::transform(extension.begin(), extension.end(), extension.begin(),
					   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

		return extension == ".vcxproj";
	}

	Core::Result<ProjectContent> VcxprojReader::Read(const std::filesystem::path& project) const
	{
		std::error_code code;

		if (!std::filesystem::is_regular_file(project, code))
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("no project file at '{}'", project.string())};
		}

		pugi::xml_document document;
		const pugi::xml_parse_result parsed = document.load_file(project.c_str());

		if (!parsed)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("cannot parse '{}': {}", project.string(),
										   parsed.description())};
		}

		// The .filters file sits beside the project with the extension appended, so
		// App.vcxproj is paired with App.vcxproj.filters.
		std::filesystem::path filtersPath = project;
		filtersPath += ".filters";

		const auto assignments = ReadFilterAssignments(filtersPath);
		const std::filesystem::path directory = project.parent_path();

		ProjectContent content;

		for (const pugi::xml_node& group : document.child("Project").children("ItemGroup"))
		{
			for (const pugi::xml_node& item : group.children())
			{
				const std::string_view name = item.name();

				if (name == "ProjectReference")
				{
					const std::filesystem::path referenced =
						Core::FromUtf8(item.attribute("Include").value());

					content.References.push_back(Core::ToUtf8(referenced.stem()));
					continue;
				}

				if (std::find(std::begin(kFileItems), std::end(kFileItems), name) == std::end(kFileItems))
				{
					continue;
				}

				const char* include = item.attribute("Include").value();

				if (include[0] == '\0')
					continue;

				ContentFile file;
				file.Path = (directory / Core::FromUtf8(include)).lexically_normal();
				file.Path.make_preferred();
				file.Name = Core::ToUtf8(file.Path.filename());

				const auto assignment = assignments.find(include);

				// No entry in the .filters file — or no .filters file at all — puts the
				// file at the project root, which is what Visual Studio shows.
				if (assignment == assignments.end())
				{
					content.Root.Files.push_back(std::move(file));
					continue;
				}

				const std::vector<std::string> parts = SplitFilter(assignment->second);

				if (parts.empty())
					content.Root.Files.push_back(std::move(file));
				else
					EnsureContentFolder(content.Root, parts).Files.push_back(std::move(file));
			}
		}

		SortContent(content.Root);

		// References are names too, and Visual Studio lists them sorted.
		std::sort(content.References.begin(), content.References.end(),
				  [](const std::string& left, const std::string& right) { return Core::CompareNames(left, right) < 0; });

		return content;
	}
}