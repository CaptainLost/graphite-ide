#include "corepch.h"

#include "Core/Sln/VcxprojReader.h"

#include "Core/Fs/FileSystem.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace core::sln
{
    namespace
    {
        /// The three item kinds that carry files. Anything else in a .vcxproj describes
        /// the build rather than its contents.
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
                    std::find_if(current->folders.begin(), current->folders.end(),
                                 [&part](const ContentFolder& f) { return f.name == part; });

                if (match != current->folders.end())
                {
                    current = &*match;
                    continue;
                }

                ContentFolder created;
                created.name = part;
                current->folders.push_back(std::move(created));
                current = &current->folders.back();
            }

            return *current;
        }

        /// Which filter each file was assigned, keyed by the Include string exactly as
        /// the .filters file spells it — the two files must agree for a lookup to hit.
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

        /// Visual Studio shows a project's contents sorted, not in the order the file
        /// happens to list them — and a .vcxproj lists every ClInclude in one group and
        /// every ClCompile in another, so preserving file order puts all the headers
        /// above all the sources. Folders first, then names, case-insensitively: the same
        /// rule the file tree uses.
        void SortContent(ContentFolder& folder)
        {
            std::sort(folder.folders.begin(), folder.folders.end(),
                      [](const ContentFolder& left, const ContentFolder& right)
                      { return fs::CompareNames(left.name, right.name) < 0; });

            std::sort(folder.files.begin(), folder.files.end(),
                      [](const ContentFile& left, const ContentFile& right)
                      { return fs::CompareNames(left.name, right.name) < 0; });

            for (ContentFolder& child : folder.folders)
                SortContent(child);
        }
    }

    bool VcxprojReader::Handles(const std::filesystem::path& project) const
    {
        // Compared case-insensitively, as ReadSolution does for the solution itself: the
        // filesystem does not care, so App.VCXPROJ must not silently render as a leaf.
        std::string extension = fs::ToUtf8(project.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });

        return extension == ".vcxproj";
    }

    Result<ProjectContent> VcxprojReader::Read(const std::filesystem::path& project) const
    {
        std::error_code code;

        if (!std::filesystem::is_regular_file(project, code))
        {
            return Error{ErrorCode::NotFound,
                         std::format("no project file at '{}'", project.string())};
        }

        pugi::xml_document document;
        const pugi::xml_parse_result parsed = document.load_file(project.c_str());

        if (!parsed)
        {
            return Error{ErrorCode::InvalidArgument,
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
                        fs::FromUtf8(item.attribute("Include").value());

                    content.references.push_back(fs::ToUtf8(referenced.stem()));
                    continue;
                }

                if (std::find(std::begin(kFileItems), std::end(kFileItems), name)
                    == std::end(kFileItems))
                {
                    continue;
                }

                const char* include = item.attribute("Include").value();

                if (include[0] == '\0')
                    continue;

                ContentFile file;
                file.path = (directory / fs::FromUtf8(include)).lexically_normal();
                file.path.make_preferred();
                file.name = fs::ToUtf8(file.path.filename());

                const auto assignment = assignments.find(include);

                // No entry in the .filters file — or no .filters file at all — puts the
                // file at the project root, which is what Visual Studio shows.
                if (assignment == assignments.end())
                {
                    content.root.files.push_back(std::move(file));
                    continue;
                }

                const std::vector<std::string> parts = SplitFilter(assignment->second);

                if (parts.empty())
                    content.root.files.push_back(std::move(file));
                else
                    EnsureContentFolder(content.root, parts).files.push_back(std::move(file));
            }
        }

        SortContent(content.root);

        // References are names too, and Visual Studio lists them sorted.
        std::sort(content.references.begin(), content.references.end(),
                  [](const std::string& left, const std::string& right)
                  { return fs::CompareNames(left, right) < 0; });

        return content;
    }
}
