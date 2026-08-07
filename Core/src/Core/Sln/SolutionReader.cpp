#include "corepch.h"

#include "Core/Sln/SolutionReader.h"

#include "Core/Fs/FileSystem.h"

#include <pugixml.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace core::sln
{
    namespace
    {
        /// Splits "/App/Sub/" into {"App", "Sub"}. The slashes in a .slnx folder name
        /// carry the nesting; the elements themselves are siblings in the document.
        std::vector<std::string> SplitFolderPath(std::string_view name)
        {
            std::vector<std::string> parts;
            std::size_t start = 0;

            while (start < name.size())
            {
                const std::size_t slash = name.find('/', start);
                const std::size_t end = slash == std::string_view::npos ? name.size() : slash;

                if (end > start)
                    parts.emplace_back(name.substr(start, end - start));

                if (slash == std::string_view::npos)
                    break;

                start = slash + 1;
            }

            return parts;
        }

        /// Finds or creates the folder chain, so a child folder declared before its
        /// parent still lands in the right place.
        Folder& EnsureFolder(Folder& root, const std::vector<std::string>& parts)
        {
            Folder* current = &root;

            for (const std::string& part : parts)
            {
                const auto match = std::find_if(current->folders.begin(), current->folders.end(),
                                                [&part](const Folder& f) { return f.name == part; });

                if (match != current->folders.end())
                {
                    current = &*match;
                    continue;
                }

                Folder created;
                created.name = part;
                current->folders.push_back(std::move(created));
                current = &current->folders.back();
            }

            return *current;
        }

        void ReadProjects(const pugi::xml_node& parent, Folder& folder,
                          const std::filesystem::path& solutionDirectory, std::size_t& count)
        {
            for (const pugi::xml_node& node : parent.children("Project"))
            {
                const char* declared = node.attribute("Path").value();

                if (declared[0] == '\0')
                    continue;

                Project project;

                // Declared relative to the solution and usually with backslashes, which
                // std::filesystem accepts on Windows. Made absolute here, once, because
                // everything downstream opens the file rather than displaying the path.
                project.path = (solutionDirectory / fs::FromUtf8(declared)).lexically_normal();
                project.path.make_preferred();

                project.displayName = fs::ToUtf8(project.path.stem());
                project.typeId = node.attribute("Type").value();

                // One stat per project, and worth it: a solution naming a file that is
                // not there is a real and common problem, and the view is meant to show
                // it rather than to quietly render an ordinary-looking node.
                std::error_code exists;
                project.exists = std::filesystem::is_regular_file(project.path, exists);

                folder.projects.push_back(std::move(project));
                ++count;
            }
        }
    }

    Result<Solution> ReadSolution(const std::filesystem::path& path)
    {
        std::error_code code;

        if (!std::filesystem::is_regular_file(path, code))
        {
            return Error{ErrorCode::NotFound,
                         std::format("no solution file at '{}'", path.string())};
        }

        if (path.extension() != ".slnx")
        {
            return Error{ErrorCode::Unsupported,
                         std::format("'{}' is not a solution format this build can read",
                                     path.string())};
        }

        pugi::xml_document document;
        const pugi::xml_parse_result parsed = document.load_file(path.c_str());

        if (!parsed)
        {
            return Error{ErrorCode::InvalidArgument,
                         std::format("cannot parse '{}': {}", path.string(),
                                     parsed.description())};
        }

        const pugi::xml_node root = document.child("Solution");

        if (!root)
        {
            return Error{ErrorCode::InvalidArgument,
                         std::format("'{}' has no Solution element", path.string())};
        }

        Solution solution;
        solution.path = path;
        solution.name = fs::ToUtf8(path.stem());

        const std::filesystem::path directory = path.parent_path();

        // Projects declared outside any Folder belong to the solution itself.
        ReadProjects(root, solution.root, directory, solution.projectCount);

        for (const pugi::xml_node& node : root.children("Folder"))
        {
            const std::vector<std::string> parts = SplitFolderPath(node.attribute("Name").value());

            if (parts.empty())
                continue;

            Folder& folder = EnsureFolder(solution.root, parts);
            ReadProjects(node, folder, directory, solution.projectCount);
        }

        return solution;
    }
}
