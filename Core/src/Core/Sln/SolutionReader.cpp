#include "corepch.h"

#include "Core/Sln/SolutionReader.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Sln/ProjectContentReader.h"
#include "Core/Sln/SlnParser.h"
#include "Core/Sln/VcxprojReader.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
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

        /// Every reader this build knows about. A project no reader claims keeps
        /// contentRead == false and shows as a leaf, so an unsupported project type never
        /// costs more than its own contents.
        void FillContent(Folder& folder)
        {
            static const VcxprojReader kVcxproj;
            static const ProjectContentReader* const kReaders[] = {&kVcxproj};

            for (Project& project : folder.projects)
            {
                for (const ProjectContentReader* reader : kReaders)
                {
                    if (!reader->Handles(project.path))
                        continue;

                    auto content = reader->Read(project.path);

                    // A project that cannot be read does not sink the solution: the
                    // structure around it is still true and still useful.
                    if (!content)
                    {
                        CORE_TRACE("Cannot read '{}': {}", project.path.string(),
                                   content.GetError().message);
                        break;
                    }

                    project.content = std::move(content).Value();
                    project.contentRead = true;
                    break;
                }
            }

            for (Folder& child : folder.folders)
                FillContent(child);
        }

        /// Visual Studio lists solution folders and projects sorted, not in the order the
        /// file declares them. Folders first, then projects, names compared
        /// case-insensitively — the same rule as the file tree and as a project's own
        /// contents, so the two views cannot disagree about where a name belongs.
        void SortSolution(Folder& folder)
        {
            std::sort(folder.folders.begin(), folder.folders.end(),
                      [](const Folder& left, const Folder& right)
                      { return fs::CompareNames(left.name, right.name) < 0; });

            std::sort(folder.projects.begin(), folder.projects.end(),
                      [](const Project& left, const Project& right)
                      { return fs::CompareNames(left.displayName, right.displayName) < 0; });

            for (Folder& child : folder.folders)
                SortSolution(child);
        }

        Result<Solution> ReadSlnx(const std::filesystem::path& path)
        {
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

        Result<Solution> ReadSlnText(const std::filesystem::path& path)
        {
            std::ifstream stream{path, std::ios::binary};

            if (!stream.is_open())
            {
                return Error{ErrorCode::IoFailure,
                             std::format("cannot open '{}'", path.string())};
            }

            std::ostringstream buffer;
            buffer << stream.rdbuf();

            return ParseSln(path, buffer.str());
        }
    }

    Result<Solution> ReadSolution(const std::filesystem::path& requested)
    {
        std::error_code code;

        if (!std::filesystem::is_regular_file(requested, code))
        {
            return Error{ErrorCode::NotFound,
                         std::format("no solution file at '{}'", requested.string())};
        }

        // Resolved once, here, rather than trusting the caller. Every project path is
        // built by appending to this one, and the model promises absolute paths — a
        // relative argument would quietly hand back relative projects that only resolve
        // while the process happens to sit in the right directory.
        std::filesystem::path path = std::filesystem::absolute(requested, code);

        if (code)
            path = requested;

        // Compared case-insensitively: the filesystem this runs on is, and rejecting
        // MyApp.SLNX as an unknown format would be a lie about a perfectly ordinary file.
        std::string extension = fs::ToUtf8(path.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });

        if (extension != ".slnx" && extension != ".sln")
        {
            return Error{ErrorCode::Unsupported,
                         std::format("'{}' is not a solution format this build can read",
                                     path.string())};
        }

        Result<Solution> parsed = extension == ".slnx" ? ReadSlnx(path) : ReadSlnText(path);

        if (!parsed)
            return parsed;

        Solution solution = std::move(parsed).Value();

        // Run after either parser, so the two formats cannot drift apart in what they
        // hand back.
        FillContent(solution.root);
        SortSolution(solution.root);

        return solution;
    }
}
