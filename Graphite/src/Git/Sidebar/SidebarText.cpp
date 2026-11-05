#include "Git/Sidebar/SidebarText.h"

#include "Git/Branches/BranchAge.h"
#include "Git/History/CommitDetails.h"

#include "Core/Base/Count.h"
#include "Core/Base/Strings.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/OwnAuthors.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>

namespace Graphite {

	namespace {

		constexpr std::int64_t kSecondsPerDay = 86400;
		constexpr const char* kSeparator = " \xC2\xB7 "; // " · "
		constexpr const char* kEllipsis = "\xE2\x80\xA6";

		std::string LastSegment(const std::string& name)
		{
			const std::size_t cut = name.rfind('/');
			return cut == std::string::npos ? name : name.substr(cut + 1);
		}

		// The segments before the last "/"; empty when the name has none.
		std::string FolderOf(const std::string& name)
		{
			const std::size_t cut = name.rfind('/');
			return cut == std::string::npos ? std::string() : name.substr(0, cut);
		}

		// "origin/x" -> "origin", the longest remote name that prefixes it; the first segment when no remote matches.
		std::string RemoteOfName(const std::string& name, const std::vector<Core::Remote>& remotes)
		{
			std::string best;

			for (const Core::Remote& remote : remotes)
			{
				if (remote.Name.size() > best.size() && name.size() > remote.Name.size() && name.starts_with(remote.Name) &&
					name[remote.Name.size()] == '/')
				{
					best = remote.Name;
				}
			}

			if (best.empty())
			{
				best = name.substr(0, name.find('/'));
			}

			return best;
		}

		struct Place
		{
			std::string Text;
			SidebarTone Tone = SidebarTone::Dim;
			std::string Gone; // the remote a deleted upstream was on; empty when none
		};

		// The remote a local branch tracks, by its upstream ref when the read has it, else by the upstream's name.
		std::string RemoteTracked(std::size_t index, const SidebarTextInput& input)
		{
			const Core::LineRef& ref = input.Read.Refs[index];

			if (ref.UpstreamFullName.has_value())
			{
				const std::optional<std::size_t> upstream = input.Lines.IndexOf(*ref.UpstreamFullName);

				if (upstream.has_value() && !input.Read.Refs[*upstream].Remote.empty())
				{
					return input.Read.Refs[*upstream].Remote;
				}
			}

			const Core::Branch& branch = (*input.Read.Branches)[index];

			if (branch.Upstream.has_value())
			{
				return RemoteOfName(*branch.Upstream, input.Read.Remotes);
			}

			return {};
		}

		// Where a ref lives, as the sidebar says it. `local` is the local branch of the row, if it has one.
		Place PlaceOf(std::size_t index, std::optional<std::size_t> local, const SidebarTextInput& input)
		{
			const Core::LineRef& ref = input.Read.Refs[index];

			if (!ref.Remote.empty())
			{
				if (local.has_value())
				{
					return Place{.Text = "local + " + ref.Remote, .Tone = SidebarTone::Both};
				}

				return Place{.Text = ref.Remote + " only", .Tone = SidebarTone::Dim};
			}

			const std::string remote = RemoteTracked(index, input);

			if (!remote.empty())
			{
				return Place{.Text = "local + " + remote, .Tone = SidebarTone::Both};
			}

			const Core::Branch& branch = (*input.Read.Branches)[index];
			Place place{.Text = "local only", .Tone = SidebarTone::Local};

			if (branch.MissingUpstream.has_value())
			{
				place.Gone = RemoteOfName(*branch.MissingUpstream, input.Read.Remotes);
			}

			return place;
		}

		// "13d ago"; "now" stands alone.
		std::string AgeWords(const Core::Branch& branch, std::chrono::sys_seconds now)
		{
			const std::string age = FormatBranchAge(branch.TipCommit->Committer.When, now);
			return age == "now" ? age : age + " ago";
		}

		std::string AuthorWords(const Core::Branch& branch, const std::optional<std::string>& userEmail)
		{
			if (userEmail.has_value() && Core::AuthorKeyOf(branch.TipCommit->Author.Email) == Core::AuthorKeyOf(*userEmail))
			{
				return "you";
			}

			return branch.TipCommit->Author.Name;
		}

		std::string DistanceWords(const Core::Branch& branch)
		{
			if (!branch.DistanceKnown)
			{
				return {};
			}

			std::string words;

			if (branch.Ahead > 0)
			{
				words = Core::FormatCount(branch.Ahead) + " to push";
			}

			if (branch.Behind > 0)
			{
				words += (words.empty() ? "" : kSeparator) + Core::FormatCount(branch.Behind) + " to pull";
			}

			return words;
		}

		std::string OldCopyNote(const Core::OldCopy& copy, const LineRead& read)
		{
			return "default here, an old copy of " + read.Refs[copy.Of].ShortName;
		}

		std::string DisplayName(const Core::LineRef& ref)
		{
			return ref.Remote.empty() ? ref.ShortName : ref.Remote + "/" + ref.ShortName;
		}

		// What a branch is compared with, for the tooltip; empty when with nothing.
		std::string ComparisonSentence(const Core::BranchLine& line, const LineRead& read)
		{
			if (!line.ComparedWith.has_value())
			{
				return {};
			}

			const std::string& name = read.Refs[*line.ComparedWith].ShortName;

			if (line.State == Core::BranchState::NoOwnCommits)
			{
				return "No commits of its own yet; it points into " + name;
			}

			if (line.State == Core::BranchState::MergedByHistory)
			{
				return "Merged into " + name;
			}

			const std::string ahead = Core::FormatCount(line.Ahead) + " ahead of " + name;
			return line.Kind == Core::ComparedKind::Branch ? "Built on " + name + ": " + ahead : ahead;
		}

		// What a base's or an old copy's row says of the line the user picked for it.
		std::string PickedLineWords(const LineRead& read, std::size_t picked)
		{
			return "vs " + read.Refs[picked].ShortName;
		}

		void AddUnusualSpans(std::vector<SidebarSpan>& meta, const Core::BranchLine* line, std::size_t index, const SidebarTextInput& input)
		{
			const LineRead& read = input.Read;

			const Core::OldCopy* copy = input.Lines.OldCopyNamed(read.Refs[index].FullName);

			// The header tells an old copy against nothing but the user's pick, so its row names no line the rule gave it,
			// and always names the pick, which is never the usual line for it.
			const std::optional<std::size_t> picked = input.Lines.PickedLineOf(read.Refs[index].FullName);

			if (picked.has_value())
			{
				meta.push_back({.Text = PickedLineWords(read, *picked), .Tone = SidebarTone::Unusual});
			}
			else if (line != nullptr)
			{
				if (line->State == Core::BranchState::NoOwnCommits)
				{
					meta.push_back({.Text = "no own commits yet", .Tone = SidebarTone::NoOwnCommits});
				}
				else if (line->State == Core::BranchState::MergedByHistory)
				{
					meta.push_back({.Text = "merged", .Tone = SidebarTone::Merged});
				}
				else if (line->ComparedWith.has_value() && copy == nullptr)
				{
					const Core::LineRef& compared = read.Refs[*line->ComparedWith];

					if (line->Kind == Core::ComparedKind::Branch)
					{
						meta.push_back({.Text = "on top of " + LastSegment(compared.ShortName), .Tone = SidebarTone::Unusual});
					}
					else if (compared.FullName != input.MostCommonLine)
					{
						meta.push_back({.Text = "vs " + compared.ShortName, .Tone = SidebarTone::Unusual});
					}
				}
			}

			if (copy != nullptr)
			{
				meta.push_back({.Text = OldCopyNote(*copy, read), .Tone = SidebarTone::Unusual});
			}

			if (line == nullptr)
			{
				return;
			}

			if (line->State == Core::BranchState::HistoryCutOff)
			{
				meta.push_back({.Text = "history cut off", .Tone = SidebarTone::Unusual});
			}
			else if (line->State == Core::BranchState::NoCommonHistory)
			{
				meta.push_back({.Text = "no common history", .Tone = SidebarTone::Unusual});
			}
			else if (line->State == Core::BranchState::TipMissing)
			{
				meta.push_back({.Text = "tip missing", .Tone = SidebarTone::Unusual});
			}
		}
	}

	std::string MostCommonLineOf(const LineRead& read)
	{
		std::map<std::size_t, std::size_t> countByRef;

		// Only a base is a line: a stack of branches built on one another must not make one of them the usual line.
		for (const Core::BranchLine& line : read.Model.Branches)
		{
			if (line.ComparedWith.has_value() && line.Kind == Core::ComparedKind::Base)
			{
				++countByRef[*line.ComparedWith];
			}
		}

		std::size_t bestRef = 0;
		std::size_t bestCount = 0;

		for (const auto& [ref, count] : countByRef)
		{
			if (count > bestCount)
			{
				bestRef = ref;
				bestCount = count;
			}
		}

		return bestCount == 0 ? std::string() : read.Refs[bestRef].FullName;
	}

	std::string BaseReasonText(const Core::LineBase& base, const LineRead& read)
	{
		switch (base.Reason)
		{
		case Core::BaseReason::ServerDefault:
			return "Default branch on " + read.Refs[base.Ref].Remote;
		case Core::BaseReason::MainLineByUser:
			return "Chosen by you as the main line";
		case Core::BaseReason::MainLineGuess:
			return "Guessed: most branches forked from its line in the last 90 days. The window asks you once to confirm.";
		case Core::BaseReason::AddedByUser:
			return "Added by you";
		case Core::BaseReason::Kept:
			return "A base once; it stays one until you stop it";
		case Core::BaseReason::MergedLines:
		case Core::BaseReason::Authors:
			break;
		}

		const Core::LineQualities& qualities = base.Qualities;

		if (qualities.MergedLines == 0 && qualities.Authors == 0 && qualities.Span == 0 && qualities.Commits == 0)
		{
			return "Found from the data";
		}

		if (base.Reason == Core::BaseReason::MergedLines)
		{
			return qualities.MergedLines == 1 ? "1 different branch merged into it in the last 90 days"
											  : Core::FormatCount(qualities.MergedLines) + " different branches merged into it in the last 90 days";
		}

		const std::int64_t days = std::max<std::int64_t>(1, qualities.Span / kSecondsPerDay);
		const std::string people = qualities.Authors == 1 ? "1 person" : Core::FormatCount(qualities.Authors) + " people";
		return people + " worked on it over " + Core::FormatCount(static_cast<std::size_t>(days)) + (days == 1 ? " day" : " days");
	}

	SidebarRowText DescribeSidebarRow(const SidebarRow& row, const SidebarTextInput& input)
	{
		SidebarRowText text;

		if ((row.Kind != SidebarRowKind::Branch && row.Kind != SidebarRowKind::Base) || !row.Ref.has_value())
		{
			return text;
		}

		const LineRead& read = input.Read;
		const std::size_t index = *row.Ref;
		const Core::LineRef& ref = read.Refs[index];
		const Core::Branch& branch = (*read.Branches)[index];
		const bool isBase = row.Kind == SidebarRowKind::Base;
		const bool inRemoteSection = row.Section == SidebarSection::Remote;

		// The local branch that carries the row's push and pull words and its checked-out mark.
		std::optional<std::size_t> local = row.LocalCopy;

		if (!local.has_value() && ref.Remote.empty())
		{
			local = index;
		}

		const Core::Branch* localBranch = local.has_value() ? &(*read.Branches)[*local] : nullptr;
		const Place place = PlaceOf(index, row.LocalCopy, input);

		text.IsHead = branch.IsHead || (localBranch != nullptr && localBranch->IsHead);
		text.Head = isBase ? ref.ShortName : LastSegment(ref.ShortName);
		text.Right = localBranch != nullptr ? DistanceWords(*localBranch) : std::string();

		if (!inRemoteSection)
		{
			text.Meta.push_back({.Text = place.Text, .Tone = place.Tone});

			if (!isBase && !place.Gone.empty())
			{
				text.Meta.push_back({.Text = "deleted on " + place.Gone, .Tone = SidebarTone::Gone});
			}
		}

		const std::string folder = isBase ? std::string() : FolderOf(ref.ShortName);

		if (!folder.empty())
		{
			text.Meta.push_back({.Text = folder, .Tone = SidebarTone::Dim});
		}

		if (branch.TipCommit.has_value())
		{
			if (!isBase)
			{
				text.Meta.push_back({.Text = AuthorWords(branch, input.UserEmail), .Tone = SidebarTone::Dim});
			}

			text.Meta.push_back({.Text = AgeWords(branch, input.Now), .Tone = SidebarTone::Dim});
		}

		const Core::BranchLine* line = isBase ? nullptr : input.Lines.LineOf(ref.FullName);
		const Core::LineBase* base = isBase ? input.Lines.BaseNamed(ref.FullName) : nullptr;

		if (base != nullptr && base->Reason == Core::BaseReason::MainLineGuess)
		{
			text.Meta.push_back({.Text = "guessed", .Tone = SidebarTone::Unusual});
		}
		else if (base != nullptr && base->Reason == Core::BaseReason::MainLineByUser)
		{
			text.Meta.push_back({.Text = "chosen by you", .Tone = SidebarTone::Dim});
		}

		if (const std::optional<std::size_t> picked = base != nullptr ? input.Lines.PickedLineOf(ref.FullName) : std::nullopt)
		{
			text.Meta.push_back({.Text = PickedLineWords(read, *picked), .Tone = SidebarTone::Unusual});
		}

		if (!isBase)
		{
			AddUnusualSpans(text.Meta, line, index, input);
		}

		std::string& tip = text.Tooltip;
		tip = DisplayName(ref);

		if (branch.TipCommit.has_value())
		{
			tip += "\n" + branch.TipCommit->Author.Name + ", " + AgeWords(branch, input.Now);
		}

		if (base != nullptr)
		{
			tip += "\nBase: " + BaseReasonText(*base, read);
		}
		else if (line != nullptr)
		{
			const std::string sentence = ComparisonSentence(*line, read);

			if (!sentence.empty())
			{
				tip += "\n" + sentence;
			}
		}

		const Core::OldCopy* copy = isBase ? nullptr : input.Lines.OldCopyNamed(ref.FullName);

		if (copy != nullptr)
		{
			tip += "\n" + OldCopyNote(*copy, read);
		}

		if (localBranch != nullptr && !inRemoteSection)
		{
			if (place.Tone == SidebarTone::Both && localBranch->DistanceKnown)
			{
				tip += "\nLocal copy: " + Core::FormatCount(localBranch->Ahead) + " to push, " + Core::FormatCount(localBranch->Behind) + " to pull";
			}
			else if (place.Tone != SidebarTone::Both)
			{
				tip += "\nLocal only, on no remote";
			}
		}

		if (!place.Gone.empty() && !inRemoteSection)
		{
			tip += "\nDeleted on " + place.Gone + "; only the local copy is left";
		}

		if (localBranch != nullptr && input.WorktreeOf)
		{
			text.Worktree = input.WorktreeOf(localBranch->FullName);

			if (text.Worktree.has_value())
			{
				tip += "\nChecked out in a worktree: " + Core::ToUtf8(*text.Worktree);

				if (input.IsDirty)
				{
					text.Dirty = input.IsDirty(*text.Worktree);
				}

				if (const std::optional<std::string> failure = input.CountFailureOf ? input.CountFailureOf(*text.Worktree) : std::nullopt)
				{
					text.CountFailure = *failure;
					tip += "\nCannot count the changes: " + *failure;
				}
			}
		}

		return text;
	}

	WorktreeDotPlace WorktreeDotPlaceOf(const SidebarRowText& text)
	{
		if (text.Dirty.value_or(false))
		{
			return WorktreeDotPlace::Dot;
		}

		if (!text.Dirty.has_value() && text.CountFailure.empty())
		{
			return WorktreeDotPlace::Waiting;
		}

		return WorktreeDotPlace::Nothing;
	}

	std::optional<SidebarWaitingRow> DescribeSidebarWait(const SidebarWaitInput& input)
	{
		if (input.HasRead)
		{
			return std::nullopt;
		}

		if (input.LinesError.has_value() && !input.Reading)
		{
			return SidebarWaitingRow{.Text = "Cannot read the branches' lines: " + input.LinesError->Message, .Failed = true};
		}

		if (input.Branches != nullptr)
		{
			const std::size_t count = input.Branches->size();
			const char* const noun = count == 1 ? " branch" : " branches";

			return SidebarWaitingRow{.Text = "Reading " + Core::FormatCount(count) + noun + kEllipsis};
		}

		if (input.BranchesError.has_value())
		{
			return SidebarWaitingRow{.Text = "Cannot read the branches: " + input.BranchesError->Message, .Failed = true};
		}

		return SidebarWaitingRow{.Text = std::string("Reading branches") + kEllipsis};
	}

	std::string SectionCountText(const SidebarRow& row)
	{
		if (row.CountFailed)
		{
			return {};
		}

		if (!row.Counted)
		{
			return kEllipsis;
		}

		if (row.Total.has_value())
		{
			return Core::FormatCount(row.Count) + " of " + Core::FormatCount(*row.Total);
		}

		return Core::FormatCount(row.Count);
	}

	SidebarRowText DescribeStashRow(const Core::StashEntry& stash, std::chrono::sys_seconds now,
									const std::chrono::time_zone* zone)
	{
		const std::chrono::sys_seconds made{std::chrono::seconds{stash.SecondsSinceEpoch}};

		SidebarRowText text;
		text.Head = std::string(Core::FirstLine(stash.Message));
		text.Right = FormatBranchAge(made, now);
		text.Tooltip = stash.Message + "\n" + FormattedTimestamp(made, zone);
		return text;
	}
}
