#include "Solution/CsprojReader.h"

#include "Core/FileSystem/FileSystem.h"

#include <pugixml.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr std::string_view kExcludedDirectories[] = {"bin", "obj"};

		bool IsExcluded(const std::string& name)
		{
			for (const std::string_view excluded : kExcludedDirectories)
			{
				if (Core::CompareNames(name, excluded) == 0)
				{
					return true;
				}
			}

			return false;
		}

		bool IsReferenceNameBefore(const std::string& left, const std::string& right)
		{
			return Core::CompareNames(left, right) < 0;
		}

		void ReadDirectoryInto(const std::filesystem::path& directory,
							   const std::filesystem::path& projectFile, ContentFolder& folder)
		{
			const Core::Result<std::vector<Core::DirectoryEntry>> entries = Core::ReadDirectory(directory);

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

				const bool isProjectItself = entry.Path == projectFile;

				if (isProjectItself)
				{
					continue;
				}

				ContentFile file;
				file.Name = entry.Name;
				file.Path = entry.Path;

				folder.Files.push_back(std::move(file));
			}
		}
	}

	bool CsprojReader::Handles(const std::filesystem::path& project) const
	{
		return Core::LoweredExtension(project) == ".csproj";
	}

	Core::Result<ProjectContent> CsprojReader::Read(const std::filesystem::path& project) const
	{
		std::error_code projectFileError;

		if (!std::filesystem::is_regular_file(project, projectFileError))
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("no project file at '{}'", Core::ToUtf8(project))};
		}

		ProjectContent content;

		pugi::xml_document document;

		if (document.load_file(project.c_str()))
		{
			for (const pugi::xml_node& itemGroup : document.child("Project").children("ItemGroup"))
			{
				for (const pugi::xml_node& item : itemGroup.children("ProjectReference"))
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
		SortContentByName(content.Root);

		std::sort(content.References.begin(), content.References.end(), IsReferenceNameBefore);

		return content;
	}
}