#include "Sln/SlnParser.h"

#include "Core/Fs/FileSystem.h"

#include <algorithm>
#include <format>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr std::string_view kFolderType = "{2150E333-8FDC-42A3-9474-1A3956D46DE8}";

		constexpr std::string_view kHeader = "Microsoft Visual Studio Solution File";

		constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";

		constexpr int kHeaderSearchLines = 4;

		struct Entry
		{
			std::string TypeId;
			std::string Name;
			std::string Location; // a relative path for a project, the name for a folder
			std::string Id;
			bool IsFolder = false;
		};

		std::string_view Trim(std::string_view text)
		{
			while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
				text.remove_prefix(1);

			while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
			{
				text.remove_suffix(1);
			}

			return text;
		}

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

		bool ParseEntry(std::string_view line, Entry& entry)
		{
			const std::vector<std::string> fields = QuotedFields(line);

			if (fields.size() < 4)
				return false;

			entry.TypeId = fields[0];
			entry.Name = fields[1];
			entry.Location = fields[2];
			entry.Id = fields[3];
			entry.IsFolder = entry.TypeId == kFolderType;

			return true;
		}
	}

	Core::Result<Solution> ParseSln(const std::filesystem::path& path, const std::string& text)
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
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("'{}' does not begin like a solution file",
										   path.string())};
		}

		std::vector<Entry> entries;
		std::unordered_map<std::string, std::string> parentOf; // child id -> parent id
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
		solution.Path = path;
		solution.Name = Core::ToUtf8(path.stem());

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
			indexById.emplace(entries[i].Id, i);

		const std::size_t count = entries.size();
		std::vector<std::vector<std::size_t>> childrenOf(count);
		std::vector<std::size_t> roots;
		std::vector<std::size_t> parentOfIndex(count, count); // count == "no parent"

		for (std::size_t i = 0; i < count; ++i)
		{
			const auto link = parentOf.find(entries[i].Id);

			if (link == parentOf.end())
				continue;

			const auto parent = indexById.find(link->second);

			// A parent that is not a folder, is not declared, or is the entry itself,
			// means the entry belongs at the solution root.
			if (parent == indexById.end() || parent->second == i || !entries[parent->second].IsFolder)
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

		const auto makeProject = [&](const Entry& entry) {
			Project project;
			project.DisplayName = entry.Name;
			project.TypeId = entry.TypeId;
			project.Path = (directory / Core::FromUtf8(entry.Location)).lexically_normal();
			project.Path.make_preferred();

			std::error_code exists;
			project.Exists = std::filesystem::is_regular_file(project.Path, exists);

			return project;
		};

		// Built by value, children first, so no reference into the tree is ever held
		// across an append.
		const auto build = [&](std::size_t index, auto&& self) -> Folder {
			Folder folder;
			folder.Name = entries[index].Name;

			for (const std::size_t child : childrenOf[index])
			{
				if (entries[child].IsFolder)
					folder.Folders.push_back(self(child, self));
				else
					folder.Projects.push_back(makeProject(entries[child]));
			}

			return folder;
		};

		for (const std::size_t index : roots)
		{
			if (entries[index].IsFolder)
				solution.Root.Folders.push_back(build(index, build));
			else
				solution.Root.Projects.push_back(makeProject(entries[index]));
		}

		// Counted from the model rather than while parsing, so an entry dropped for any
		// reason cannot leave the count disagreeing with the tree.
		const auto countProjects = [](const Folder& folder, auto&& self) -> std::size_t {
			std::size_t total = folder.Projects.size();

			for (const Folder& child : folder.Folders)
				total += self(child, self);

			return total;
		};

		solution.ProjectCount = countProjects(solution.Root, countProjects);

		return solution;
	}
}