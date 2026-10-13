#include "Review/ReviewTree.h"

#include "Git/History/CommitLabels.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string_view>

namespace Graphite {

	namespace {

		constexpr char kPathSeparator = '/';

		struct FolderNode
		{
			std::string Name;
			std::filesystem::path Path;
			std::vector<FolderNode> Folders;

			// Position in Folders by name, so finding a child stays logarithmic with thousands of siblings.
			std::map<std::string, std::size_t> FolderPositions;
			std::vector<std::size_t> Files;
			std::size_t Viewed = 0;
			std::size_t Total = 0;
			bool HasShownFile = false;
		};

		struct EmitContext
		{
			ReviewGroup Group = ReviewGroup::Commits;
			const Core::Diff* Diff = nullptr;
			const std::vector<bool>* Shown = nullptr;
			bool IgnoreFolds = false;
			const std::set<MarkKey>* FoldedFolders = nullptr;
			std::vector<ReviewTreeRow>* Rows = nullptr;
		};

		std::string LowerAscii(std::string_view text)
		{
			std::string lowered;
			lowered.reserve(text.size());

			for (const char character : text)
			{
				lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
			}

			return lowered;
		}

		std::vector<std::string> FolderNamesOf(const std::filesystem::path& path)
		{
			std::vector<std::string> names;

			for (const std::filesystem::path& part : path.parent_path())
			{
				names.push_back(Core::ToUtf8(part));
			}

			return names;
		}

		FolderNode& ChildFolderNamed(FolderNode& parent, const std::string& name)
		{
			if (parent.FolderPositions.contains(name))
			{
				return parent.Folders[parent.FolderPositions.at(name)];
			}

			parent.FolderPositions.emplace(name, parent.Folders.size());
			parent.Folders.push_back(FolderNode{.Name = name, .Path = parent.Path / Core::FromUtf8(name)});
			return parent.Folders.back();
		}

		void AddFile(FolderNode& root, const std::filesystem::path& path, std::size_t fileIndex, bool viewed,
					 bool shown)
		{
			FolderNode* folder = &root;

			for (const std::string& name : FolderNamesOf(path))
			{
				folder = &ChildFolderNamed(*folder, name);
				++folder->Total;

				if (viewed)
				{
					++folder->Viewed;
				}

				if (shown)
				{
					folder->HasShownFile = true;
				}
			}

			folder->Files.push_back(fileIndex);
		}

		bool IsFolderNamedBefore(const FolderNode* left, const FolderNode* right)
		{
			return left->Name < right->Name;
		}

		bool IsFileNamedBefore(const Core::Diff& diff, std::size_t leftIndex, std::size_t rightIndex)
		{
			return ShownPath(diff.Files[leftIndex]).filename() < ShownPath(diff.Files[rightIndex]).filename();
		}

		bool IsFilePathBefore(const Core::Diff& diff, std::size_t leftIndex, std::size_t rightIndex)
		{
			return ShownPath(diff.Files[leftIndex]) < ShownPath(diff.Files[rightIndex]);
		}

		bool IsFolded(const EmitContext& context, const std::filesystem::path& folderPath)
		{
			if (context.IgnoreFolds || context.FoldedFolders == nullptr)
			{
				return false;
			}

			return context.FoldedFolders->contains(MarkKey{.Group = context.Group, .Path = folderPath});
		}

		void EmitFiles(const EmitContext& context, std::vector<std::size_t> files, std::size_t depth)
		{
			const Core::Diff& diff = *context.Diff;
			std::sort(files.begin(), files.end(),
					  [&diff](std::size_t left, std::size_t right) { return IsFileNamedBefore(diff, left, right); });

			for (const std::size_t fileIndex : files)
			{
				if (!(*context.Shown)[fileIndex])
				{
					continue;
				}

				context.Rows->push_back(
					ReviewTreeRow{.Kind = TreeRowKind::File, .Group = context.Group, .Depth = depth, .FileIndex = fileIndex});
			}
		}

		void EmitFolder(const EmitContext& context, const FolderNode& folder, std::size_t depth);

		void EmitChildren(const EmitContext& context, const FolderNode& folder, std::size_t depth)
		{
			std::vector<const FolderNode*> shownFolders;

			for (const FolderNode& child : folder.Folders)
			{
				if (child.HasShownFile)
				{
					shownFolders.push_back(&child);
				}
			}

			std::sort(shownFolders.begin(), shownFolders.end(), IsFolderNamedBefore);

			for (const FolderNode* child : shownFolders)
			{
				EmitFolder(context, *child, depth);
			}

			EmitFiles(context, folder.Files, depth);
		}

		void EmitFolder(const EmitContext& context, const FolderNode& folder, std::size_t depth)
		{
			const FolderNode* joined = &folder;
			std::string label = folder.Name;

			while (joined->Files.empty() && joined->Folders.size() == 1)
			{
				joined = &joined->Folders.front();
				label += kPathSeparator;
				label += joined->Name;
			}

			const bool folded = IsFolded(context, joined->Path);

			context.Rows->push_back(ReviewTreeRow{.Kind = TreeRowKind::Folder,
												  .Group = context.Group,
												  .Depth = depth,
												  .Label = label,
												  .FolderPath = joined->Path,
												  .Viewed = joined->Viewed,
												  .Total = joined->Total,
												  .Folded = folded});

			if (folded)
			{
				return;
			}

			EmitChildren(context, *joined, depth + 1);
		}

		void EmitFlat(const EmitContext& context, std::size_t depth)
		{
			const Core::Diff& diff = *context.Diff;
			std::vector<std::size_t> files;
			files.reserve(diff.Files.size());

			for (std::size_t fileIndex = 0; fileIndex < diff.Files.size(); ++fileIndex)
			{
				files.push_back(fileIndex);
			}

			std::sort(files.begin(), files.end(),
					  [&diff](std::size_t left, std::size_t right) { return IsFilePathBefore(diff, left, right); });

			for (const std::size_t fileIndex : files)
			{
				if ((*context.Shown)[fileIndex])
				{
					context.Rows->push_back(
						ReviewTreeRow{.Kind = TreeRowKind::File, .Group = context.Group, .Depth = depth, .FileIndex = fileIndex});
				}
			}
		}

		void AppendGroup(const ReviewTreeInput& input, ReviewGroup group, const Core::Diff& diff,
						 std::vector<ReviewTreeRow>& rows)
		{
			const bool notShownYet = false;
			std::vector<bool> shown(diff.Files.size(), notShownYet);
			FolderNode root;
			std::size_t shownCount = 0;
			std::size_t viewedCount = 0;

			for (std::size_t fileIndex = 0; fileIndex < diff.Files.size(); ++fileIndex)
			{
				const Core::FileDiff& file = diff.Files[fileIndex];
				const ViewedState state = input.StateOf ? input.StateOf(group, file) : ViewedState::NotViewed;
				const bool viewed = state == ViewedState::Viewed;
				const bool hiddenAsViewed = input.HideViewed && viewed;
				const bool passes = MatchesFilter(ShownPath(file), input.Filter) && !hiddenAsViewed;

				shown[fileIndex] = passes;

				if (passes)
				{
					++shownCount;
				}

				if (viewed)
				{
					++viewedCount;
				}

				AddFile(root, ShownPath(file), fileIndex, viewed, passes);
			}

			std::size_t depth = 0;

			if (input.Headings == GroupHeadings::Shown)
			{
				const bool collapsed = input.Groups.Collapsed[IndexOf(group)];

				rows.push_back(ReviewTreeRow{.Kind = TreeRowKind::Heading,
											 .Group = group,
											 .Depth = 0,
											 .Viewed = viewedCount,
											 .Total = diff.Files.size(),
											 .Shown = shownCount,
											 .Folded = collapsed});

				if (collapsed)
				{
					return;
				}

				depth = 1;
			}

			const bool filtering = !input.Filter.empty() || input.HideViewed;
			const EmitContext context{.Group = group,
									  .Diff = &diff,
									  .Shown = &shown,
									  .IgnoreFolds = filtering,
									  .FoldedFolders = input.FoldedFolders,
									  .Rows = &rows};

			if (input.Shape == ListShape::Flat)
			{
				EmitFlat(context, depth);
				return;
			}

			EmitChildren(context, root, depth);
		}

		std::vector<ReviewSelection> FileSelectionsOf(const ReviewGroups& groups, const std::vector<ReviewTreeRow>& rows)
		{
			std::vector<ReviewSelection> selections;

			for (const ReviewTreeRow& row : rows)
			{
				if (row.Kind == TreeRowKind::File)
				{
					selections.push_back(ReviewSelection{.Group = row.Group, .Path = ShownPath(FileOfTreeRow(groups, row))});
				}
			}

			return selections;
		}

		std::optional<std::size_t> PositionOf(const std::vector<ReviewSelection>& selections,
											  const ReviewSelection& wanted)
		{
			for (std::size_t position = 0; position < selections.size(); ++position)
			{
				if (selections[position] == wanted)
				{
					return position;
				}
			}

			return std::nullopt;
		}

		// The order BuildReviewTree lists files in: groups in turn, then by path in the flat shape; in the tree,
		// by name at the first folder where the paths part, and a file inside a further folder before a file
		// beside that folder, as folders come before files.
		bool IsListedBefore(const ReviewSelection& left, const ReviewSelection& right, ListShape shape)
		{
			if (left.Group != right.Group)
			{
				return IndexOf(left.Group) < IndexOf(right.Group);
			}

			if (shape == ListShape::Flat)
			{
				return left.Path < right.Path;
			}

			const std::vector<std::string> leftFolders = FolderNamesOf(left.Path);
			const std::vector<std::string> rightFolders = FolderNamesOf(right.Path);
			const std::size_t sharedDepth = std::min(leftFolders.size(), rightFolders.size());

			for (std::size_t depth = 0; depth < sharedDepth; ++depth)
			{
				if (leftFolders[depth] != rightFolders[depth])
				{
					return leftFolders[depth] < rightFolders[depth];
				}
			}

			if (leftFolders.size() != rightFolders.size())
			{
				return leftFolders.size() > rightFolders.size();
			}

			return left.Path.filename() < right.Path.filename();
		}

		std::optional<ReviewSelection> FirstListedAfter(const ReviewSelection& unlisted,
														const std::vector<ReviewSelection>& files, ListShape shape)
		{
			for (const ReviewSelection& file : files)
			{
				if (IsListedBefore(unlisted, file, shape))
				{
					return file;
				}
			}

			return std::nullopt;
		}

		std::optional<ReviewSelection> LastListedBefore(const ReviewSelection& unlisted,
														const std::vector<ReviewSelection>& files, ListShape shape)
		{
			for (std::size_t position = files.size(); position > 0; --position)
			{
				const ReviewSelection& file = files[position - 1];

				if (IsListedBefore(file, unlisted, shape))
				{
					return file;
				}
			}

			return std::nullopt;
		}
	}

	bool MatchesFilter(const std::filesystem::path& path, const std::string& filter)
	{
		if (filter.empty())
		{
			return true;
		}

		return LowerAscii(Core::ToGenericUtf8(path)).find(LowerAscii(filter)) != std::string::npos;
	}

	std::vector<ReviewTreeRow> BuildReviewTree(const ReviewTreeInput& input)
	{
		std::vector<ReviewTreeRow> rows;

		if (input.Groups.Commits != nullptr)
		{
			AppendGroup(input, ReviewGroup::Commits, *input.Groups.Commits, rows);
		}

		if (input.Groups.Uncommitted != nullptr)
		{
			AppendGroup(input, ReviewGroup::Uncommitted, *input.Groups.Uncommitted, rows);
		}

		return rows;
	}

	const Core::FileDiff& FileOfTreeRow(const ReviewGroups& groups, const ReviewTreeRow& row)
	{
		return DiffOf(groups, row.Group)->Files[row.FileIndex];
	}

	std::optional<ReviewSelection> FirstFileOf(const ReviewGroups& groups, const std::vector<ReviewTreeRow>& rows)
	{
		const std::vector<ReviewSelection> files = FileSelectionsOf(groups, rows);

		if (files.empty())
		{
			return std::nullopt;
		}

		return files.front();
	}

	std::optional<ReviewSelection> NeighbourFileOf(const std::optional<ReviewSelection>& selected,
												   const ReviewGroups& groups,
												   const std::vector<ReviewTreeRow>& rows, ListStep step, ListShape shape)
	{
		const std::vector<ReviewSelection> files = FileSelectionsOf(groups, rows);

		if (files.empty())
		{
			return selected;
		}

		if (!selected.has_value())
		{
			return files.front();
		}

		const std::optional<std::size_t> position = PositionOf(files, *selected);

		if (!position.has_value())
		{
			const std::optional<ReviewSelection> neighbour = step == ListStep::Next
																 ? FirstListedAfter(*selected, files, shape)
																 : LastListedBefore(*selected, files, shape);
			return neighbour.has_value() ? neighbour : selected;
		}

		const std::size_t lastPosition = files.size() - 1;

		if (step == ListStep::Next)
		{
			return files[std::min(*position + 1, lastPosition)];
		}

		const bool atTop = *position == 0;
		return files[atTop ? 0 : *position - 1];
	}
}
