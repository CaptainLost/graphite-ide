#include "Solution/CsprojReader.h"

#include "Core/FileSystem/FileSystem.h"

#include <pugixml.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::string_view kExcludedDirectories[] = {"bin", "obj"};

		bool IsExcluded(const std::string& name)
		{
			return std::any_of(std::begin(kExcludedDirectories), std::end(kExcludedDirectories),
							   [&name](std::string_view excluded) { return Core::CompareNames(name, excluded) == 0; });
		}

		void ReadDirectoryInto(const std::filesystem::path& directory,
							   const std::filesystem::path& projectFile, ContentFolder& folder)
		{
			const auto entries = Core::ReadDirectory(directory);

			if (!entries)
			{
				return;
			}

			for (const Core::DirectoryEntry& entry : entries.GetValue())
			{
				if (entry.IsDirectory)
				{
					if (IsExcluded(entry.Name))
					{
						continue;
					}

					ContentFolder child;
					child.Name = entry.Name;

					ReadDirectoryInto(entry.Path, projectFile, child);
					folder.Folders.push_back(std::move(child));
					continue;
				}

				// Visual Studio shows the project as the node, not as a file inside itself.
				if (entry.Path == projectFile)
				{
					continue;
				}

				ContentFile file;
				file.Name = entry.Name;
				file.Path = entry.Path;

				folder.Files.push_back(std::move(file));
			}
		}

		void SortContent(ContentFolder& folder)
		{
			std::sort(folder.Folders.begin(), folder.Folders.end(),
					  [](const ContentFolder& left, const ContentFolder& right) { return Core::CompareNames(left.Name, right.Name) < 0; });

			std::sort(folder.Files.begin(), folder.Files.end(),
					  [](const ContentFile& left, const ContentFile& right) { return Core::CompareNames(left.Name, right.Name) < 0; });

			for (ContentFolder& child : folder.Folders)
			{
				SortContent(child);
			}
		}
	}

	bool CsprojReader::Handles(const std::filesystem::path& project) const
	{
		return Core::LoweredExtension(project) == ".csproj";
	}

	Core::Result<ProjectContent> CsprojReader::Read(const std::filesystem::path& project) const
	{
		std::error_code code;

		if (!std::filesystem::is_regular_file(project, code))
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("no project file at '{}'", Core::ToUtf8(project))};
		}

		ProjectContent content;

		// An SDK-style project does not list its files, so only the references come from
		// the document and the tree comes from the disk.
		pugi::xml_document document;

		if (document.load_file(project.c_str()))
		{
			for (const pugi::xml_node& group : document.child("Project").children("ItemGroup"))
			{
				for (const pugi::xml_node& item : group.children("ProjectReference"))
				{
					const std::filesystem::path referenced =
						Core::FromUtf8(item.attribute("Include").value());

					if (!referenced.empty())
					{
						content.References.push_back(Core::ToUtf8(referenced.stem()));
					}
				}
			}
		}

		ReadDirectoryInto(project.parent_path(), project, content.Root);
		SortContent(content.Root);

		std::sort(content.References.begin(), content.References.end(),
				  [](const std::string& left, const std::string& right) { return Core::CompareNames(left, right) < 0; });

		return content;
	}
}