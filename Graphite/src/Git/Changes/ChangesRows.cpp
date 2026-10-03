#include "Git/Changes/ChangesRows.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <format>

namespace Graphite {

	namespace {
		bool Has(Core::StatusFlags flags, Core::StatusFlags wanted)
		{
			return (flags & wanted) == wanted;
		}

		// Whether `path` sits strictly inside `directory`, compared as Core compares every path.
		// A directory does not contain itself, and an empty directory is not a directory every
		// path happens to be under.
		bool UnderDirectory(const std::filesystem::path& path,
							const std::filesystem::path& directory)
		{
			const auto relative = Core::RelativeTo(path, directory);
			return relative.has_value() && *relative != ".";
		}
	}

	ChangeSections PartitionStatus(const Core::WorkingTreeStatus& status)
	{
		ChangeSections sections;

		for (std::size_t index = 0; index < status.Entries.size(); ++index)
		{
			const Core::FileStatus& file = status.Entries[index];

			if (file.IsConflicted)
			{
				sections.Conflicted.push_back(index);
				continue;
			}

			if (file.Index != Core::StatusFlags::None)
			{
				sections.Staged.push_back(index);
			}

			if (file.WorkTree != Core::StatusFlags::None)
			{
				sections.Unstaged.push_back(index);
			}
		}

		return sections;
	}

	char ChangeLetter(Core::StatusFlags flags, bool untracked)
	{
		// Untracked first: not an A, because the difference matters here - discarding one
		// deletes it rather than restoring it. A letter rather than the ?? git status prints,
		// since every other mark in the list is a letter and a lone question mark among them
		// reads as something the panel could not work out.
		if (untracked)
		{
			return 'U';
		}

		// Ordered by what a reader most needs told. A rename beats a modification because
		// the path on screen has already changed and nothing else would explain it; a
		// deletion beats both because the file is not there at all.
		if (Has(flags, Core::StatusFlags::Deleted))
		{
			return 'D';
		}

		if (Has(flags, Core::StatusFlags::Renamed))
		{
			return 'R';
		}

		if (Has(flags, Core::StatusFlags::New))
		{
			return 'A';
		}

		if (Has(flags, Core::StatusFlags::TypeChanged))
		{
			return 'T';
		}

		if (Has(flags, Core::StatusFlags::Modified))
		{
			return 'M';
		}

		return ' ';
	}

	std::optional<std::filesystem::path> NextSelection(
		const std::filesystem::path& selected,
		const std::vector<std::filesystem::path>& before,
		const std::vector<std::filesystem::path>& after)
	{
		if (std::find(after.begin(), after.end(), selected) != after.end())
		{
			return selected;
		}

		if (after.empty())
		{
			return std::nullopt;
		}

		const auto was = std::find(before.begin(), before.end(), selected);

		if (was == before.end())
		{
			return std::nullopt;
		}

		const std::size_t position = static_cast<std::size_t>(was - before.begin());

		return after[std::min(position, after.size() - 1)];
	}

	DiscardPlan PlanDiscard(const DiscardSubject& subject,
							const std::filesystem::path& workingDirectory,
							const std::vector<std::filesystem::path>& openDirty)
	{
		DiscardPlan plan;

		// An untracked path has nothing in the index to be restored from, so discarding it
		// can only mean deleting it - a different outcome, and one the row's icon already
		// said.
		if (subject.IsUntracked)
		{
			plan.Delete.push_back(subject.Path);
		}
		else
		{
			plan.Restore.push_back(subject.Path);
		}

		if (subject.IsDirectory)
		{
			if (subject.Listing.has_value())
			{
				plan.Files = *subject.Listing;
				plan.FileCount = plan.Files.size();
			}

			// Without a listing the files stay unknown rather than guessed. Reading the
			// directory off the disk here would answer the question git is being asked
			// instead of, and a guess put in front of the reader is worse than a sentence
			// that does not count.
		}
		else
		{
			plan.Files.push_back(subject.Path);
		}

		const std::filesystem::path absoluteSubject =
			Core::NormalizePath(workingDirectory / subject.Path);

		for (const std::filesystem::path& document : openDirty)
		{
			const std::filesystem::path normalized = Core::NormalizePath(document);

			// Not the listing: a directory nobody opened has no listing, and the edits under
			// it would be lost all the same. The subject answers for every file beneath it,
			// listed or not.
			if (Core::SamePath(normalized, absoluteSubject) || (subject.IsDirectory && UnderDirectory(normalized, absoluteSubject)))
			{
				plan.Dirty.push_back(document);
			}
		}

		return plan;
	}

	std::string DiscardQuestionText(const DiscardSubject& subject, const DiscardPlan& plan)
	{
		const std::string name = Core::ToUtf8(subject.Path);

		if (subject.IsDirectory)
		{
			if (plan.FileCount.has_value())
			{
				return std::format("Delete {}/ and the {} files in it?", name, *plan.FileCount);
			}

			return std::format("Delete {}/ and everything in it?", name);
		}

		if (!plan.Delete.empty())
		{
			return std::format("Delete {}?", name);
		}

		// Unstaged, not "since the last commit": a discard restores from the index - see
		// Repository::DiscardWorkTree - so a partially staged file keeps what was staged and
		// loses only the edits made on top of it. Naming the larger loss would be naming a
		// loss that is not about to happen.
		return std::format("Discard the unstaged changes to {}?", name);
	}

	RowText FitRowText(float available, float name, float directory, float gap,
					   float minDirectory)
	{
		if (available <= 0.0f)
		{
			return {};
		}

		RowText text;
		text.Name = std::min(name, available);

		const float left = available - text.Name - gap;

		if (directory > 0.0f && left >= std::min(minDirectory, directory))
		{
			text.Directory = std::min(directory, left);
		}

		return text;
	}
}
