#include "corepch.h"

#include "Core/Sln/SlnParser.h"

#include "Core/Fs/FileSystem.h"

#include <algorithm>
#include <format>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace core::sln
{
    namespace
    {
        /// The type GUID Visual Studio gives a solution folder. Every other type is a
        /// real project, whatever language it is.
        constexpr std::string_view kFolderType = "{2150E333-8FDC-42A3-9474-1A3956D46DE8}";

        /// What the first line must start with. Checked so that a text file which merely
        /// ends in .sln reports an error instead of reading as an empty solution.
        constexpr std::string_view kHeader = "Microsoft Visual Studio Solution File";

        /// One `Project(...)` entry, before nesting is known.
        struct Entry
        {
            std::string typeId;
            std::string name;
            std::string location;   // a relative path for a project, the name for a folder
            std::string id;
            bool isFolder = false;
        };

        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);

            while (!text.empty()
                   && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
            {
                text.remove_suffix(1);
            }

            return text;
        }

        /// Pulls the quoted fields out of a line in declaration order. The format puts
        /// every field of interest in quotes, so this is enough to parse an entry without
        /// a grammar.
        std::vector<std::string> QuotedFields(std::string_view line)
        {
            std::vector<std::string> fields;
            std::size_t position = 0;

            while (true)
            {
                const std::size_t open = line.find('"', position);

                if (open == std::string_view::npos)
                    break;

                const std::size_t close = line.find('"', open + 1);

                if (close == std::string_view::npos)
                    break;

                fields.emplace_back(line.substr(open + 1, close - open - 1));
                position = close + 1;
            }

            return fields;
        }

        /// A `Project("{type}") = "name", "location", "{id}"` line yields four fields.
        /// Anything else is a line this parser has no use for.
        bool ParseEntry(std::string_view line, Entry& entry)
        {
            const std::vector<std::string> fields = QuotedFields(line);

            if (fields.size() < 4)
                return false;

            entry.typeId = fields[0];
            entry.name = fields[1];
            entry.location = fields[2];
            entry.id = fields[3];
            entry.isFolder = entry.typeId == kFolderType;

            return true;
        }
    }

    Result<Solution> ParseSln(const std::filesystem::path& path, const std::string& text)
    {
        std::istringstream stream{text};
        std::string rawLine;

        if (!std::getline(stream, rawLine) || Trim(rawLine).find(kHeader) != 0)
        {
            return Error{ErrorCode::InvalidArgument,
                         std::format("'{}' does not begin like a solution file",
                                     path.string())};
        }

        std::vector<Entry> entries;
        std::unordered_map<std::string, std::string> parentOf;   // child id -> parent id
        bool inNesting = false;

        while (std::getline(stream, rawLine))
        {
            const std::string_view line = Trim(rawLine);

            if (line.starts_with("Project("))
            {
                if (Entry entry; ParseEntry(line, entry))
                    entries.push_back(std::move(entry));

                // A line that does not parse is skipped rather than fatal: hand-edited
                // and merge-conflicted solutions exist, and one bad line should cost that
                // line rather than the whole view.
                continue;
            }

            if (line.starts_with("GlobalSection(NestedProjects)"))
            {
                inNesting = true;
                continue;
            }

            if (line.starts_with("EndGlobalSection"))
            {
                inNesting = false;
                continue;
            }

            if (!inNesting)
                continue;

            // `{child} = {parent}`, and nothing else appears in this section.
            const std::size_t equals = line.find('=');

            if (equals == std::string_view::npos)
                continue;

            const std::string_view child = Trim(line.substr(0, equals));
            const std::string_view parent = Trim(line.substr(equals + 1));

            if (!child.empty() && !parent.empty())
                parentOf.emplace(child, parent);
        }

        Solution solution;
        solution.path = path;
        solution.name = fs::ToUtf8(path.stem());

        const std::filesystem::path directory = path.parent_path();

        // Two passes, because nesting is declared apart from the entries and may name a
        // parent that appears later in the file. Folders are materialised first so that
        // every parent exists before anything is placed into it.
        std::unordered_map<std::string, Folder*> folderById;

        // Depth-limited walk: a folder's parent chain is followed to find where it goes,
        // and a cycle in a corrupt file would otherwise loop for ever.
        const auto placeFolder = [&](const Entry& entry, auto&& self) -> Folder*
        {
            if (const auto found = folderById.find(entry.id); found != folderById.end())
                return found->second;

            Folder* parent = &solution.root;

            if (const auto link = parentOf.find(entry.id); link != parentOf.end())
            {
                const auto parentEntry =
                    std::find_if(entries.begin(), entries.end(),
                                 [&link](const Entry& candidate)
                                 { return candidate.isFolder && candidate.id == link->second; });

                if (parentEntry != entries.end() && parentEntry->id != entry.id)
                    parent = self(*parentEntry, self);
            }

            Folder created;
            created.name = entry.name;
            parent->folders.push_back(std::move(created));

            Folder* placed = &parent->folders.back();
            folderById.emplace(entry.id, placed);

            return placed;
        };

        for (const Entry& entry : entries)
        {
            if (entry.isFolder)
                (void)placeFolder(entry, placeFolder);
        }

        for (const Entry& entry : entries)
        {
            if (entry.isFolder)
                continue;

            Folder* parent = &solution.root;

            if (const auto link = parentOf.find(entry.id); link != parentOf.end())
            {
                if (const auto found = folderById.find(link->second); found != folderById.end())
                    parent = found->second;
            }

            Project project;
            project.displayName = entry.name;
            project.typeId = entry.typeId;
            project.path = (directory / fs::FromUtf8(entry.location)).lexically_normal();
            project.path.make_preferred();

            std::error_code exists;
            project.exists = std::filesystem::is_regular_file(project.path, exists);

            parent->projects.push_back(std::move(project));
            ++solution.projectCount;
        }

        return solution;
    }
}
