#include "Git/History/CommitDetails.h"

#include "Git/Compare/VsLabel.h"
#include "Git/Tags/FirstTag.h"

#include "Core/Base/Count.h"
#include "Core/Time/TimeZone.h"

#include <algorithm>
#include <format>

namespace Graphite {

	DiffStats CountChanges(const Core::Diff& diff)
	{
		DiffStats changeCounts;
		changeCounts.Files = diff.Files.size();

		for (const Core::FileDiff& fileDiff : diff.Files)
		{
			const Core::LineCounts lines = Core::CountLines(fileDiff);

			changeCounts.Added += lines.Added;
			changeCounts.Removed += lines.Removed;
		}

		return changeCounts;
	}

	std::string UncountedChangesText(bool diffFailed, std::string_view failure)
	{
		if (!diffFailed)
		{
			return "counting changes\xE2\x80\xA6";
		}

		return failure.empty() ? std::string{"couldn't count changes"} : "couldn't count changes: " + std::string{failure};
	}

	std::string FormattedTimestamp(std::chrono::sys_seconds instant, std::chrono::minutes offset)
	{
		return std::format("{:%Y-%m-%d %H:%M}", instant + offset);
	}

	std::string FormattedTimestamp(std::chrono::sys_seconds instant, const std::chrono::time_zone* zone)
	{
		return FormattedTimestamp(instant, Core::OffsetAt(zone, instant));
	}

	std::string CommitMessageText(const Core::CommitSummary& commit)
	{
		return commit.Body.empty() ? commit.Summary : commit.Summary + "\n\n" + commit.Body;
	}

	bool ShowsCommitter(const Core::CommitSummary& commit)
	{
		return commit.Committer.Name != commit.Author.Name ||
			   commit.Committer.Email != commit.Author.Email;
	}

	InBases InBasesWithoutRead(bool reading, const std::optional<Core::Error>& linesError,
							   const std::optional<Core::Error>& branchesError)
	{
		InBases bases;

		if (linesError.has_value())
		{
			bases.Unknown = linesError->Message;
		}
		else if (reading)
		{
			bases.Pending = true;
		}
		else if (branchesError.has_value())
		{
			bases.Unknown = branchesError->Message;
		}
		else
		{
			bases.Pending = true;
		}

		return bases;
	}

	std::size_t InBasesChipsThatFit(const std::vector<float>& chipWidths, float gap, float room, float moreWidth)
	{
		std::size_t fitting = 0;
		float used = 0.0f;

		for (std::size_t chip = 0; chip < chipWidths.size(); ++chip)
		{
			used += (chip == 0 ? 0.0f : gap) + chipWidths[chip];

			// The last chip needs no words after it, so it can fit where the one before it with the words did not.
			const bool isLast = chip + 1 == chipWidths.size();
			const float needed = isLast ? used : used + gap + moreWidth;

			if (needed <= room)
			{
				fitting = chip + 1;
			}
		}

		return fitting;
	}

	std::string InBasesMoreText(std::size_t shown, std::size_t total)
	{
		if (shown >= total)
		{
			return {};
		}

		if (shown == 0)
		{
			return Core::FormatCount(total) + (total == 1 ? " base" : " bases");
		}

		return "and " + Core::FormatCount(total - shown) + " more";
	}

	FirstTagLine DescribeFirstTag(const LineRead* read, const Core::Ref<const std::vector<Core::Tag>>& listed,
								  const Core::Oid& commit, const std::optional<Core::Error>& linesError)
	{
		FirstTagLine line;
		line.Shown = RepositoryHasTags(read, listed);

		if (!line.Shown)
		{
			return line;
		}

		if (read == nullptr)
		{
			// A failure stays said while a read is asked again after it, as In bases says it.
			if (linesError.has_value())
			{
				line.Unknown = linesError->Message;
			}
			else
			{
				line.Pending = true;
			}

			return line;
		}

		if (listed != nullptr && read->Tags != listed)
		{
			line.Pending = true;
			return line;
		}

		if (const Core::Tag* tag = FirstTagOf(*read, commit))
		{
			line.Name = tag->Name;
			line.When = tag->When;
		}

		return line;
	}

	InBases InBasesOutsideRead(bool reading, const std::optional<Core::Error>& linesError)
	{
		InBases bases;
		bases.Pending = reading && !linesError.has_value();

		return bases;
	}

	std::optional<TwinMark> TwinFromShownSquash(const ContentMark& shown, const Core::Oid& commit)
	{
		if (!IsMergedByContent(shown) || shown.Verdict->Kind != Core::ContentKind::Squash)
		{
			return std::nullopt;
		}

		for (const Core::PatchTwin& twin : shown.Verdict->Twins)
		{
			if (twin.Own == commit)
			{
				return TwinMark{.State = ContentMark::State::Known, .Twin = twin.Twin};
			}
		}

		// Not searched for, so not known either way: the squash's chip says the line holds the change.
		return TwinMark{.State = ContentMark::State::Unknown, .Twin = std::nullopt};
	}

	InBases DescribeInBases(const LineRead& read, const std::optional<std::vector<bool>>& holding,
							const std::vector<BaseEquivalence>& equivalence)
	{
		InBases bases;

		if (!holding.has_value())
		{
			bases.Pending = true;
			return bases;
		}

		const std::size_t known = std::min(holding->size(), read.Model.Bases.size());

		for (std::size_t index = 0; index < known; ++index)
		{
			if ((*holding)[index])
			{
				bases.Holding.push_back(LineNameOf(read, read.Model.Bases[index].Ref));
			}
		}

		for (const BaseEquivalence& base : equivalence)
		{
			if (base.Base >= read.Model.Bases.size())
			{
				continue;
			}

			const std::string name = LineNameOf(read, read.Model.Bases[base.Base].Ref);

			if (base.Twin.State == ContentMark::State::Known && base.Twin.Twin.has_value())
			{
				bases.Equivalents.push_back(InBaseEquivalent{.Base = name, .Text = "≈ copy"});
			}
			else if (base.SquashLanding.has_value())
			{
				bases.Equivalents.push_back(
					InBaseEquivalent{.Base = name, .Text = "≈ in squash " + base.SquashLanding->ShortHex()});
			}
			else if (base.Twin.State == ContentMark::State::Pending || base.SquashPending)
			{
				bases.EquivalentsPending = true;
			}
		}

		return bases;
	}

	std::string InBasesEmptyText(const InBases& bases)
	{
		return bases.EquivalentsPending ? "…" : "in no base yet";
	}

	const std::optional<std::string>& KeptSourceSentence::Keep(const Core::Oid& commit, const std::optional<std::string>& current)
	{
		if (!m_For.has_value() || *m_For != commit)
		{
			m_For = commit;
			m_Text = current;
		}

		return m_Text;
	}

	void KeptSourceSentence::Forget()
	{
		m_For.reset();
		m_Text.reset();
	}

	std::optional<std::string> ContentSourceSentence(const LineRead& read, const ContentSource& source)
	{
		for (std::size_t ref = 0; ref < read.Refs.size(); ++ref)
		{
			if (read.Refs[ref].FullName != source.Branch)
			{
				continue;
			}

			const std::string name = LineNameOf(read, ref);

			if (source.Own.has_value())
			{
				return "Copy of " + source.Own->ShortHex() + " from " + name + ".";
			}

			return "Squash of " + name + ".";
		}

		return std::nullopt;
	}
}
