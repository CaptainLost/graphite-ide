#include "corepch.h"

#include "Core/Sln/CsprojReader.h"

#include "Core/Fs/FileSystem.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Core
{
    namespace
    {
        /// What the .NET SDK leaves out of its default glob. Every project has both and
        /// nobody wants to browse either.
        constexpr std::string_view kExcludedDirectories[] = {"bin", "obj"};

        bool IsExcluded(const std::string& name)
        {
            return std::any_of(std::begin(kExcludedDirectories), std::end(kExcludedDirectories),
                               [&name](std::string_view excluded)
                               { return CompareNames(name, excluded) == 0; });
        }

        /// Mirrors one directory into the content tree, skipping build output and the
        /// project file itself.
        void ReadDirectoryInto(const std::filesystem::path& directory,
                               const std::filesystem::path& projectFile, ContentFolder& folder)
        {
            const auto entries = ReadDirectory(directory);

            if (!entries)
                return;

            for (const DirectoryEntry& entry : entries.Value())
            {
                if (entry.isDirectory)
                {
                    if (IsExcluded(entry.name))
                        continue;

                    ContentFolder child;
                    child.name = entry.name;

                    ReadDirectoryInto(entry.path, projectFile, child);
                    folder.folders.push_back(std::move(child));
                    continue;
                }

                // Visual Studio shows the project as the node, not as a file inside
                // itself.
                if (entry.path == projectFile)
                    continue;

                ContentFile file;
                file.name = entry.name;
                file.path = entry.path;

                folder.files.push_back(std::move(file));
            }
        }

        void SortContent(ContentFolder& folder)
        {
            std::sort(folder.folders.begin(), folder.folders.end(),
                      [](const ContentFolder& left, const ContentFolder& right)
                      { return CompareNames(left.name, right.name) < 0; });

            std::sort(folder.files.begin(), folder.files.end(),
                      [](const ContentFile& left, const ContentFile& right)
                      { return CompareNames(left.name, right.name) < 0; });

            for (ContentFolder& child : folder.folders)
                SortContent(child);
        }
    }

    bool CsprojReader::Handles(const std::filesystem::path& project) const
    {
        std::string extension = ToUtf8(project.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });

        return extension == ".csproj";
    }

    Result<ProjectContent> CsprojReader::Read(const std::filesystem::path& project) const
    {
        std::error_code code;

        if (!std::filesystem::is_regular_file(project, code))
        {
            return Error{ErrorCode::NotFound,
                         std::format("no project file at '{}'", project.string())};
        }

        ProjectContent content;

        // References come from the document; everything else comes from the disk.
        pugi::xml_document document;

        if (document.load_file(project.c_str()))
        {
            for (const pugi::xml_node& group : document.child("Project").children("ItemGroup"))
            {
                for (const pugi::xml_node& item : group.children("ProjectReference"))
                {
                    const std::filesystem::path referenced =
                        FromUtf8(item.attribute("Include").value());

                    if (!referenced.empty())
                        content.references.push_back(ToUtf8(referenced.stem()));
                }
            }
        }

        ReadDirectoryInto(project.parent_path(), project, content.root);
        SortContent(content.root);

        std::sort(content.references.begin(), content.references.end(),
                  [](const std::string& left, const std::string& right)
                  { return CompareNames(left, right) < 0; });

        return content;
    }
}
