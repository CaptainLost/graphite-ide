#include "Solution/SlnParser.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Encoding.h"

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

		constexpr int kHeaderSearchLines = 4;

		constexpr std::size_t kFieldsPerEntry = 4;
		constexpr std::size_t kTypeIdField = 0;
		constexpr std::size_t kNameField = 1;
		constexpr std::size_t kLocationField = 2;
		constexpr std::size_t kIdField = 3;

		struct Entry
		{
			std::string TypeId;
			std::string Name;
			std::string Location;
			std::string Id;
			bool IsFolder = false;
		};

		std::string_view TrimmedSolutionLine(std::string_view text)
		{
			return Core::WithoutTrailing(Core::WithoutLeading(text, " \t"), " \t\r");
		}

		std::vector<std::string> QuotedFields(std::string_view line)
		{
			std::vector<std::string> fields;
			std::size_t position = 0;

			std::size_t openingQuote = line.find('"', position);

			while (openingQuote != std::string_view::npos)
			{
				const std::size_t closingQuote = line.find('"', openingQuote + 1);

				if (closingQuote == std::string_view::npos)
				{
					break;
				}

				fields.emplace_back(line.substr(openingQuote + 1, closingQuote - openingQuote - 1));
				position = closingQuote + 1;
				openingQuote = line.find('"', position);
			}

			return fields;
		}

		bool ParseEntry(std::string_view line, Entry& entry)
		{
			const std::vector<std::string> fields = QuotedFields(line);

			if (fields.size() < kFieldsPerEntry)
			{
				return false;
			}

			entry.TypeId = fields[kTypeIdField];
			entry.Name = fields[kNameField];
			entry.Location = fields[kLocationField];
			entry.Id = fields[kIdField];
			entry.IsFolder = entry.TypeId == kFolderType;

			return true;
		}

		// WARNING: Visual Studio writes a UTF-8 byte-order mark and then a blank line before the
		// header, so the header is neither on line one nor at column zero.
		bool ConsumeSolutionHeader(std::istream& stream)
		{
			std::string rawLine;

			for (int lineNumber = 0; lineNumber < kHeaderSearchLines && std::getline(stream, rawLine);
				 ++lineNumber)
			{
				const std::string_view candidate =
					Core::WithoutUtf8ByteOrderMark(TrimmedSolutionLine(rawLine));

				if (candidate.empty())
				{
					continue;
				}

				return candidate.starts_with(kHeader);
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
				const std::string_view line = TrimmedSolutionLine(rawLine);

				if (line.starts_with("Project("))
				{
					// WARNING: A line that does not parse costs that line rather than the whole view,
					// because hand-edited and merge-conflicted solutions exist.
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

				const std::size_t equalsPosition = line.find('=');

				if (equalsPosition == std::string_view::npos)
				{
					continue;
				}

				const std::string_view childId = TrimmedSolutionLine(line.substr(0, equalsPosition));
				const std::string_view parentId = TrimmedSolutionLine(line.substr(equalsPosition + 1));

				if (!childId.empty() && !parentId.empty())
				{
					body.ParentIdByChildId.emplace(childId, parentId);
				}
			}

			return body;
		}

		std::vector<std::size_t> ResolveParents(const Body& body)
		{
			const std::size_t entryCount = body.Entries.size();
			const std::size_t noParent = entryCount;

			std::unordered_map<std::string, std::size_t> indexById;

			for (std::size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
			{
				indexById.emplace(body.Entries[entryIndex].Id, entryIndex);
			}

			std::vector<std::size_t> parentOfIndex(entryCount, noParent);

			for (std::size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
			{
				const std::unordered_map<std::string, std::string>::const_iterator link =
					body.ParentIdByChildId.find(body.Entries[entryIndex].Id);

				if (link == body.ParentIdByChildId.end())
				{
					continue;
				}

				const auto& [childIdOfLink, parentIdOfLink] = *link;

				const std::unordered_map<std::string, std::size_t>::const_iterator parent =
					indexById.find(parentIdOfLink);

				if (parent == indexById.end())
				{
					continue;
				}

				const auto& [parentId, parentIndex] = *parent;
				const bool isOwnParent = parentIndex == entryIndex;

				if (isOwnParent || !body.Entries[parentIndex].IsFolder)
				{
					continue;
				}

				parentOfIndex[entryIndex] = parentIndex;
			}

			return parentOfIndex;
		}

		// WARNING: Cycles are detected against the untouched links and applied afterwards.
		// Rewriting as it walks would let the first entry freed from a cycle make the second
		// one's chain look sound.
		void RootEntriesInCycles(std::vector<std::size_t>& parentOfIndex)
		{
			const std::size_t entryCount = parentOfIndex.size();
			const std::size_t noParent = entryCount;

			const bool startsOutsideCycle = false;
			std::vector<bool> inCycle(entryCount, startsOutsideCycle);

			for (std::size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
			{
				std::size_t ancestor = parentOfIndex[entryIndex];

				for (std::size_t step = 0; ancestor != noParent && step <= entryCount; ++step)
				{
					ancestor = parentOfIndex[ancestor];
				}

				inCycle[entryIndex] = ancestor != noParent;
			}

			for (std::size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
			{
				if (inCycle[entryIndex])
				{
					parentOfIndex[entryIndex] = noParent;
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

		Project ProjectFromEntry(const Entry& entry, const std::filesystem::path& solutionDirectory)
		{
			Project project;
			project.DisplayName = entry.Name;
			project.TypeId = entry.TypeId;
			project.Path = (solutionDirectory / Core::FromUtf8(entry.Location)).lexically_normal();
			project.Path.make_preferred();

			std::error_code existsError;
			project.Exists = std::filesystem::is_regular_file(project.Path, existsError);

			return project;
		}

		// WARNING: Built by value, children first. A map of id to Folder* appended through would
		// dangle the moment a sibling append reallocated the vector holding it.
		Folder BuildFolder(std::size_t entryIndex, const std::vector<Entry>& entries,
						   const std::vector<std::vector<std::size_t>>& childrenOf,
						   const std::filesystem::path& solutionDirectory)
		{
			Folder folder;
			folder.Name = entries[entryIndex].Name;

			for (const std::size_t childIndex : childrenOf[entryIndex])
			{
				if (entries[childIndex].IsFolder)
				{
					folder.Folders.push_back(BuildFolder(childIndex, entries, childrenOf, solutionDirectory));
				}
				else
				{
					folder.Projects.push_back(ProjectFromEntry(entries[childIndex], solutionDirectory));
				}
			}

			return folder;
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

		const std::filesystem::path solutionDirectory = path.parent_path();

		const std::size_t entryCount = entries.size();
		const std::size_t noParent = entryCount;

		std::vector<std::size_t> parentOfIndex = ResolveParents(body);

		RootEntriesInCycles(parentOfIndex);

		std::vector<std::vector<std::size_t>> childrenOf(entryCount);
		std::vector<std::size_t> roots;

		for (std::size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
		{
			if (parentOfIndex[entryIndex] == noParent)
			{
				roots.push_back(entryIndex);
			}
			else
			{
				childrenOf[parentOfIndex[entryIndex]].push_back(entryIndex);
			}
		}

		for (const std::size_t rootIndex : roots)
		{
			if (entries[rootIndex].IsFolder)
			{
				solution.Root.Folders.push_back(BuildFolder(rootIndex, entries, childrenOf, solutionDirectory));
			}
			else
			{
				solution.Root.Projects.push_back(ProjectFromEntry(entries[rootIndex], solutionDirectory));
			}
		}

		solution.ProjectCount = CountProjects(solution.Root);

		return solution;
	}
}
