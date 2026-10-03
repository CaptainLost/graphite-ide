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
			// A relative path for a project, the name again for a folder.
			std::string Location;
			std::string Id;
			bool IsFolder = false;
		};

		std::string_view Trim(std::string_view text)
		{
			while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
			{
				text.remove_prefix(1);
			}

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
				{
					break;
				}

				const std::size_t close = line.find('"', open + 1);

				if (close == std::string_view::npos)
				{
					break;
				}

				fields.emplace_back(line.substr(open + 1, close - open - 1));
				position = close + 1;
			}

			return fields;
		}

		bool ParseEntry(std::string_view line, Entry& entry)
		{
			const std::vector<std::string> fields = QuotedFields(line);

			if (fields.size() < 4)
			{
				return false;
			}

			entry.TypeId = fields[0];
			entry.Name = fields[1];
			entry.Location = fields[2];
			entry.Id = fields[3];
			entry.IsFolder = entry.TypeId == kFolderType;

			return true;
		}

		bool ConsumeSolutionHeader(std::istream& stream)
		{
			std::string rawLine;

			// Visual Studio writes a UTF-8 byte-order mark and then a blank line before the
			// header, so the header is neither on line one nor at column zero.
			for (int line = 0; line < kHeaderSearchLines && std::getline(stream, rawLine); ++line)
			{
				std::string_view candidate = Trim(rawLine);

				if (candidate.starts_with(kByteOrderMark))
				{
					candidate.remove_prefix(kByteOrderMark.size());
				}

				if (candidate.empty())
				{
					continue;
				}

				return candidate.find(kHeader) == 0;
			}

			return false;
		}

		struct Body
		{
			std::vector<Entry> Entries;
			std::unordered_map<std::string, std::string> ParentIdByChildId;
		};

		Body ReadBody(std::istream& stream)
		{
			Body body;
			std::string rawLine;
			bool inNesting = false;

			while (std::getline(stream, rawLine))
			{
				const std::string_view line = Trim(rawLine);

				if (line.starts_with("Project("))
				{
					// A line that does not parse costs that line rather than the whole view:
					// hand-edited and merge-conflicted solutions exist.
					if (Entry entry; ParseEntry(line, entry))
					{
						body.Entries.push_back(std::move(entry));
					}

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
				{
					continue;
				}

				// Every line of this section reads `{child} = {parent}`, and nothing else.
				const std::size_t equals = line.find('=');

				if (equals == std::string_view::npos)
				{
					continue;
				}

				const std::string_view child = Trim(line.substr(0, equals));
				const std::string_view parent = Trim(line.substr(equals + 1));

				if (!child.empty() && !parent.empty())
				{
					body.ParentIdByChildId.emplace(child, parent);
				}
			}

			return body;
		}

		std::vector<std::size_t> ResolveParents(const Body& body)
		{
			const std::size_t count = body.Entries.size();
			const std::size_t noParent = count;

			std::unordered_map<std::string, std::size_t> indexById;

			for (std::size_t i = 0; i < count; ++i)
			{
				indexById.emplace(body.Entries[i].Id, i);
			}

			std::vector<std::size_t> parentOfIndex(count, noParent);

			for (std::size_t i = 0; i < count; ++i)
			{
				const auto link = body.ParentIdByChildId.find(body.Entries[i].Id);

				if (link == body.ParentIdByChildId.end())
				{
					continue;
				}

				const auto parent = indexById.find(link->second);

				if (parent == indexById.end() || parent->second == i ||
					!body.Entries[parent->second].IsFolder)
				{
					continue;
				}

				parentOfIndex[i] = parent->second;
			}

			return parentOfIndex;
		}

		void RootEntriesInCycles(std::vector<std::size_t>& parentOfIndex)
		{
			const std::size_t count = parentOfIndex.size();
			const std::size_t noParent = count;

			// Detected against the untouched links and applied afterwards: rewriting as it
			// walks would let the first entry freed from a cycle make the second one's chain
			// look sound.
			std::vector<bool> inCycle(count, false);

			for (std::size_t i = 0; i < count; ++i)
			{
				std::size_t walker = parentOfIndex[i];

				for (std::size_t steps = 0; walker != noParent && steps <= count; ++steps)
				{
					walker = parentOfIndex[walker];
				}

				inCycle[i] = walker != noParent;
			}

			for (std::size_t i = 0; i < count; ++i)
			{
				if (inCycle[i])
				{
					parentOfIndex[i] = noParent;
				}
			}
		}

		std::size_t CountProjects(const Folder& folder)
		{
			std::size_t total = folder.Projects.size();

			for (const Folder& child : folder.Folders)
			{
				total += CountProjects(child);
			}

			return total;
		}
	}

	Core::Result<Solution> ParseSln(const std::filesystem::path& path, const std::string& text)
	{
		std::istringstream stream{text};

		if (!ConsumeSolutionHeader(stream))
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("'{}' does not begin like a solution file",
										   Core::ToUtf8(path))};
		}

		const Body body = ReadBody(stream);
		const std::vector<Entry>& entries = body.Entries;

		Solution solution;
		solution.Path = path;
		solution.Name = Core::ToUtf8(path.stem());

		const std::filesystem::path directory = path.parent_path();

		const std::size_t count = entries.size();
		const std::size_t noParent = count;

		std::vector<std::size_t> parentOfIndex = ResolveParents(body);

		RootEntriesInCycles(parentOfIndex);

		std::vector<std::vector<std::size_t>> childrenOf(count);
		std::vector<std::size_t> roots;

		for (std::size_t i = 0; i < count; ++i)
		{
			if (parentOfIndex[i] == noParent)
			{
				roots.push_back(i);
			}
			else
			{
				childrenOf[parentOfIndex[i]].push_back(i);
			}
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

		// Built by value, children first: a map of id to Folder* appended through would
		// dangle the moment a sibling append reallocated the vector holding it.
		const auto buildFolder = [&](std::size_t index, auto&& self) -> Folder {
			Folder folder;
			folder.Name = entries[index].Name;

			for (const std::size_t child : childrenOf[index])
			{
				if (entries[child].IsFolder)
				{
					folder.Folders.push_back(self(child, self));
				}
				else
				{
					folder.Projects.push_back(makeProject(entries[child]));
				}
			}

			return folder;
		};

		for (const std::size_t index : roots)
		{
			if (entries[index].IsFolder)
			{
				solution.Root.Folders.push_back(buildFolder(index, buildFolder));
			}
			else
			{
				solution.Root.Projects.push_back(makeProject(entries[index]));
			}
		}

		solution.ProjectCount = CountProjects(solution.Root);

		return solution;
	}
}