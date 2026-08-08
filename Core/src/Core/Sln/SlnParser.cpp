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

        /// UTF-8 byte-order mark, which Visual Studio writes at the start of a .sln.
        constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";

        /// How far in to look for the header before giving up. Visual Studio puts a blank
        /// line before it; nothing legitimate puts more than a couple.
        constexpr int kHeaderSearchLines = 4;

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

        // Visual Studio writes a UTF-8 byte-order mark and then a blank line before the
        // header, so a parser that demands the header on line one at column zero cannot
        // open a file Visual Studio wrote — which is the only kind that matters here.
        bool headerSeen = false;

        for (int line = 0; line < kHeaderSearchLines && std::getline(stream, rawLine); ++line)
        {
            std::string_view candidate = Trim(rawLine);

            if (candidate.starts_with(kByteOrderMark))
                candidate.remove_prefix(kByteOrderMark.size());

            if (candidate.empty())
                continue;

            if (candidate.find(kHeader) == 0)
            {
                headerSeen = true;
                break;
            }

            // A non-empty line that is not the header means this is some other file.
            break;
        }

        if (!headerSeen)
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

        // Nothing here holds a pointer into the model while it is being built. An earlier
        // version kept a map of id to Folder* and appended through it, which looked safe
        // because each folder is appended once — but a *sibling* append reallocates the
        // same vector, so any solution with two folders at one level wrote through a
        // dangling pointer. Our own solution has three.
        //
        // Instead the shape is worked out entirely in index space, and the model is built
        // once, bottom-up, by value.
        std::unordered_map<std::string, std::size_t> indexById;

        for (std::size_t i = 0; i < entries.size(); ++i)
            indexById.emplace(entries[i].id, i);

        const std::size_t count = entries.size();
        std::vector<std::vector<std::size_t>> childrenOf(count);
        std::vector<std::size_t> roots;
        std::vector<std::size_t> parentOfIndex(count, count);   // count == "no parent"

        for (std::size_t i = 0; i < count; ++i)
        {
            const auto link = parentOf.find(entries[i].id);

            if (link == parentOf.end())
                continue;

            const auto parent = indexById.find(link->second);

            // A parent that is not a folder, is not declared, or is the entry itself,
            // means the entry belongs at the solution root.
            if (parent == indexById.end() || parent->second == i || !entries[parent->second].isFolder)
                continue;

            parentOfIndex[i] = parent->second;
        }

        // Anything whose parent chain does not reach the root is in a cycle — two folders
        // naming each other, which a hand-edited file can produce. Walking the chain with
        // a step limit turns that into "belongs at the root" instead of a hang.
        //
        // Detected against the untouched links and applied afterwards. Rewriting as it
        // walks would let the first entry freed from a cycle make the second one's chain
        // look sound, so of two folders naming each other one would end up inside the
        // other instead of both standing at the root.
        std::vector<bool> orphaned(count, false);

        for (std::size_t i = 0; i < count; ++i)
        {
            std::size_t walker = parentOfIndex[i];

            for (std::size_t steps = 0; walker != count && steps <= count; ++steps)
                walker = parentOfIndex[walker];

            orphaned[i] = walker != count;
        }

        for (std::size_t i = 0; i < count; ++i)
        {
            if (orphaned[i])
                parentOfIndex[i] = count;
        }

        for (std::size_t i = 0; i < count; ++i)
        {
            if (parentOfIndex[i] == count)
                roots.push_back(i);
            else
                childrenOf[parentOfIndex[i]].push_back(i);
        }

        const auto makeProject = [&](const Entry& entry)
        {
            Project project;
            project.displayName = entry.name;
            project.typeId = entry.typeId;
            project.path = (directory / fs::FromUtf8(entry.location)).lexically_normal();
            project.path.make_preferred();

            std::error_code exists;
            project.exists = std::filesystem::is_regular_file(project.path, exists);

            return project;
        };

        // Built by value, children first, so no reference into the tree is ever held
        // across an append.
        const auto build = [&](std::size_t index, auto&& self) -> Folder
        {
            Folder folder;
            folder.name = entries[index].name;

            for (const std::size_t child : childrenOf[index])
            {
                if (entries[child].isFolder)
                    folder.folders.push_back(self(child, self));
                else
                    folder.projects.push_back(makeProject(entries[child]));
            }

            return folder;
        };

        for (const std::size_t index : roots)
        {
            if (entries[index].isFolder)
                solution.root.folders.push_back(build(index, build));
            else
                solution.root.projects.push_back(makeProject(entries[index]));
        }

        // Counted from the model rather than while parsing, so an entry dropped for any
        // reason cannot leave the count disagreeing with the tree.
        const auto countProjects = [](const Folder& folder, auto&& self) -> std::size_t
        {
            std::size_t total = folder.projects.size();

            for (const Folder& child : folder.folders)
                total += self(child, self);

            return total;
        };

        solution.projectCount = countProjects(solution.root, countProjects);

        return solution;
    }
}
