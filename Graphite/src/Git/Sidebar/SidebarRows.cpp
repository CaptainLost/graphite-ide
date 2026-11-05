#include "Git/Sidebar/SidebarRows.h"

#include "Git/Branches/BranchListFilter.h"
#include "Git/Lines/BranchLines.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Count.h"

#include <algorithm>
#include <array>

namespace Graphite {

	namespace {

		constexpr std::chrono::hours kActiveWeek = std::chrono::hours{7 * 24};
		constexpr std::chrono::hours kLastNinetyDays = std::chrono::hours{90 * 24};

		constexpr bool kOnItsWay = true;
		constexpr bool kNothingOnItsWay = false;

		struct ActivityGroup
		{
			std::string_view Label;
			std::chrono::hours Younger; // tip age below this; the last group takes the rest
		};

		constexpr std::array<ActivityGroup, 3> kActivityGroups = {{
			{"Active this week", kActiveWeek},
			{"Last 90 days", kLastNinetyDays},
			{"Older than 90 days", std::chrono::hours::max()},
		}};

		// A branch with the age of its tip; one whose tip was not read has none and sorts last.
		struct Listed
		{
			std::size_t Ref = 0;
			std::optional<std::chrono::sys_seconds> TipTime;
		};

		bool IsNewer(const Listed& left, const Listed& right)
		{
			if (left.TipTime.has_value() != right.TipTime.has_value())
			{
				return left.TipTime.has_value();
			}

			return left.TipTime.has_value() && *left.TipTime > *right.TipTime;
		}

		class Builder
		{
		public:
			explicit Builder(const SidebarInput& input)
				: m_Input(input)
				, m_Lines(input.Read)
				, m_Words(LoweredWordsOf(input.Search))
				, m_Searching(!m_Words.empty())
				, m_Shown(input.ShownRef.empty() ? std::nullopt : m_Lines.IndexOf(input.ShownRef))
			{
				IndexCopies();
			}

			std::vector<SidebarRow> Build()
			{
				AddYours();
				AddBases();

				for (const Core::Remote& remote : m_Input.Read.Remotes)
				{
					AddRemote(remote.Name);
				}

				AddStashes();
				return std::move(m_Rows);
			}

			std::vector<SidebarRow> BuildStashes()
			{
				AddStashes();
				return std::move(m_Rows);
			}

		private:
			const std::vector<Core::Branch>& Branches() const { return *m_Input.Read.Branches; }

			const Core::LineRef& RefAt(std::size_t index) const { return m_Input.Read.Refs[index]; }

			bool IsLocal(std::size_t index) const { return RefAt(index).Remote.empty(); }

			void IndexCopies()
			{
				const LineRead& read = m_Input.Read;
				m_IsBase.assign(read.Refs.size(), false);
				m_LocalCopyOf.assign(read.Refs.size(), std::nullopt);
				m_BaseOwnsRow.assign(read.Refs.size(), false);

				for (const Core::LineBase& base : read.Model.Bases)
				{
					m_IsBase[base.Ref] = true;
				}

				for (const Core::LineBase& base : read.Model.Bases)
				{
					// A base that is itself a local branch is one row with the remote branch it tracks.
					const Core::LineRef& ref = read.Refs[base.Ref];

					if (IsLocal(base.Ref) && ref.UpstreamFullName.has_value())
					{
						const std::optional<std::size_t> upstream = m_Lines.IndexOf(*ref.UpstreamFullName);

						if (upstream.has_value())
						{
							m_BaseOwnsRow[*upstream] = true;
						}
					}
				}

				for (const Core::BranchLine& line : read.Model.Branches)
				{
					if (line.SameRowAs.has_value() && IsLocal(line.Ref) && !m_LocalCopyOf[*line.SameRowAs].has_value())
					{
						m_LocalCopyOf[*line.SameRowAs] = line.Ref;
					}
				}
			}

			bool IsLocalCopyOfBase(const Core::BranchLine& line) const
			{
				return line.SameRowAs.has_value() && m_IsBase[*line.SameRowAs];
			}

			// The row of `ref` is the shown branch's: the ref itself, or a base whose local copy is shown.
			bool HoldsShown(std::size_t ref) const
			{
				return m_Shown.has_value() && (ref == *m_Shown || m_LocalCopyOf[ref] == m_Shown);
			}

			bool Matches(std::size_t ref) const
			{
				if (!m_Searching)
				{
					return true;
				}

				static const BranchAuthors kNoAuthors;
				const BranchAuthors& authors = m_Input.Authors != nullptr ? *m_Input.Authors : kNoAuthors;
				const Core::Branch& branch = Branches()[ref];

				return MatchesWords(branch, AuthorsOfBranch(branch, authors), m_Words, false);
			}

			Listed ListedOf(std::size_t ref) const
			{
				const Core::Branch& branch = Branches()[ref];
				Listed listed{.Ref = ref, .TipTime = std::nullopt};

				if (branch.TipCommit.has_value())
				{
					listed.TipTime = branch.TipCommit->Committer.When;
				}

				return listed;
			}

			bool OpenOf(const std::string& key, bool byDefault) const
			{
				if (m_Searching)
				{
					return true;
				}

				const std::map<std::string, bool>::const_iterator found = m_Input.OpenByUser.find(key);
				return found != m_Input.OpenByUser.end() ? found->second : byDefault;
			}

			// Whether the section's rows follow.
			bool AddSection(SidebarSection section, std::string key, std::string label, std::size_t count, std::size_t total,
							std::string remote = {}, std::string note = {})
			{
				SidebarRow row;
				row.Kind = SidebarRowKind::Section;
				row.Section = section;
				row.Open = OpenOf(key, true);
				row.Key = std::move(key);
				row.Label = std::move(label);
				row.Note = std::move(note);
				row.Count = count;
				row.Remote = std::move(remote);

				if (m_Searching)
				{
					row.Total = total;
				}

				const bool open = row.Open;
				m_Rows.push_back(std::move(row));
				return open;
			}

			void AddBranchRow(SidebarSection section, std::size_t ref, std::size_t depth)
			{
				SidebarRow row;
				row.Kind = SidebarRowKind::Branch;
				row.Section = section;
				row.Ref = ref;
				row.Depth = depth;
				m_Rows.push_back(std::move(row));
			}

			void AddGroup(SidebarSection section, std::string key, std::string label, std::size_t count, bool open, std::string remote)
			{
				SidebarRow row;
				row.Kind = SidebarRowKind::Group;
				row.Section = section;
				row.Key = std::move(key);
				row.Label = std::move(label);
				row.Count = count;
				row.Open = open;
				row.Remote = std::move(remote);
				m_Rows.push_back(std::move(row));
			}

			void AddLink(SidebarSection section, SidebarLink link, std::string label, std::string remote = {})
			{
				SidebarRow row;
				row.Kind = SidebarRowKind::Link;
				row.Section = section;
				row.Link = link;
				row.Label = std::move(label);
				row.Remote = std::move(remote);
				m_Rows.push_back(std::move(row));
			}

			void AddYours()
			{
				std::vector<Listed> all;

				for (const Core::BranchLine& line : m_Input.Read.Model.Branches)
				{
					if (IsLocal(line.Ref) && !IsLocalCopyOfBase(line))
					{
						all.push_back(ListedOf(line.Ref));
					}
				}

				std::stable_sort(all.begin(), all.end(), IsNewer);
				std::stable_partition(all.begin(), all.end(), [this](const Listed& listed) { return Branches()[listed.Ref].IsHead; });

				std::vector<std::size_t> shown;

				for (const Listed& listed : all)
				{
					if (Matches(listed.Ref))
					{
						shown.push_back(listed.Ref);
					}
				}

				if (m_Searching && shown.empty())
				{
					return;
				}

				if (AddSection(SidebarSection::Yours, kYoursSectionKey, "Yours", shown.size(), all.size()))
				{
					for (const std::size_t ref : shown)
					{
						AddBranchRow(SidebarSection::Yours, ref, 0);
					}
				}
			}

			void AddBases()
			{
				const std::vector<Core::LineBase>& bases = m_Input.Read.Model.Bases;
				std::vector<const Core::LineBase*> active;
				std::vector<const Core::LineBase*> quiet;

				for (const Core::LineBase& base : bases)
				{
					if (Matches(base.Ref))
					{
						(base.Active ? active : quiet).push_back(&base);
					}
				}

				const std::size_t shownCount = active.size() + quiet.size();

				if (bases.empty() || (m_Searching && shownCount == 0))
				{
					return;
				}

				if (!AddSection(SidebarSection::Bases, "bases", "Bases", shownCount, bases.size()))
				{
					return;
				}

				for (const Core::LineBase* base : active)
				{
					AddBaseRow(*base, 0);
				}

				if (!quiet.empty())
				{
					bool holdsShown = false;

					for (const Core::LineBase* base : quiet)
					{
						holdsShown = holdsShown || HoldsShown(base->Ref);
					}

					const std::string key = "bases/quiet";
					const bool open = OpenOf(key, holdsShown);
					AddGroup(SidebarSection::Bases, key, "Quiet for 90 days", quiet.size(), open, {});

					if (open)
					{
						for (const Core::LineBase* base : quiet)
						{
							AddBaseRow(*base, 1);
						}
					}
				}

				if (!m_Searching)
				{
					AddLink(SidebarSection::Bases, SidebarLink::WhyBases, "Why these, and how to change them");
				}
			}

			void AddBaseRow(const Core::LineBase& base, std::size_t depth)
			{
				SidebarRow row;
				row.Kind = SidebarRowKind::Base;
				row.Section = SidebarSection::Bases;
				row.Ref = base.Ref;
				row.Depth = depth;
				row.LocalCopy = m_LocalCopyOf[base.Ref];
				m_Rows.push_back(std::move(row));
			}

			void AddRemote(const std::string& remote)
			{
				const std::string key = "remote/" + remote;
				const bool showingMerged = m_Input.ShowingMerged.contains(remote);
				const bool mergedHidden = !showingMerged && !m_Searching;

				std::size_t total = 0;
				std::size_t alsoLocal = 0;
				std::size_t hiddenMerged = 0;
				std::vector<Listed> shown;

				for (const Core::BranchLine& line : m_Input.Read.Model.Branches)
				{
					if (RefAt(line.Ref).Remote != remote || m_BaseOwnsRow[line.Ref])
					{
						continue;
					}

					++total;

					if (!Matches(line.Ref))
					{
						continue;
					}

					if (m_LocalCopyOf[line.Ref].has_value())
					{
						++alsoLocal;
					}
					else if (mergedHidden && line.State == Core::BranchState::MergedByHistory && !HoldsShown(line.Ref))
					{
						++hiddenMerged;
					}
					else
					{
						shown.push_back(ListedOf(line.Ref));
					}
				}

				if (m_Searching && shown.empty())
				{
					return;
				}

				std::stable_sort(shown.begin(), shown.end(), IsNewer);

				if (!AddSection(SidebarSection::Remote, key, remote, shown.size(), total, remote, "not on this computer"))
				{
					return;
				}

				AddRemoteBranches(remote, key, shown);

				if (alsoLocal > 0)
				{
					AddLink(SidebarSection::Remote, SidebarLink::ToYours,
							Core::FormatCount(alsoLocal) + " more you have locally \xC2\xB7 in Yours", remote);
				}

				if (hiddenMerged > 0)
				{
					AddLink(SidebarSection::Remote, SidebarLink::ShowMerged,
							Core::FormatCount(hiddenMerged) + " merged hidden \xC2\xB7 Show", remote);
				}

				if (showingMerged && !m_Searching)
				{
					AddLink(SidebarSection::Remote, SidebarLink::HideMerged, "Hide merged", remote);
				}
			}

			void AddRemoteBranches(const std::string& remote, const std::string& key, const std::vector<Listed>& shown)
			{
				if (shown.size() <= kFlatGroupMost)
				{
					for (const Listed& listed : shown)
					{
						AddBranchRow(SidebarSection::Remote, listed.Ref, 0);
					}

					return;
				}

				bool firstGroup = true;

				for (std::size_t group = 0; group < kActivityGroups.size(); ++group)
				{
					std::vector<std::size_t> members;
					bool holdsShown = false;

					for (const Listed& listed : shown)
					{
						if (ActivityGroupOf(listed) == group)
						{
							members.push_back(listed.Ref);
							holdsShown = holdsShown || HoldsShown(listed.Ref);
						}
					}

					if (members.empty())
					{
						continue;
					}

					const std::string groupKey = key + "/" + std::to_string(group);
					const bool open = OpenOf(groupKey, firstGroup || holdsShown);
					firstGroup = false;

					AddGroup(SidebarSection::Remote, groupKey, std::string{kActivityGroups[group].Label}, members.size(), open, remote);

					if (open)
					{
						for (const std::size_t ref : members)
						{
							AddBranchRow(SidebarSection::Remote, ref, 1);
						}
					}
				}
			}

			std::size_t ActivityGroupOf(const Listed& listed) const
			{
				if (!listed.TipTime.has_value())
				{
					return kActivityGroups.size() - 1;
				}

				// Against the read's time, not the clock's: a branch moves to an older group only with a new read.
				const std::chrono::sys_seconds readAt{std::chrono::seconds{m_Input.Read.Now}};
				const std::chrono::seconds age = readAt - *listed.TipTime;

				for (std::size_t group = 0; group + 1 < kActivityGroups.size(); ++group)
				{
					if (age < kActivityGroups[group].Younger)
					{
						return group;
					}
				}

				return kActivityGroups.size() - 1;
			}

			void AddStashes()
			{
				std::vector<std::size_t> shown;
				std::size_t total = 0;

				if (m_Input.Stashes != nullptr)
				{
					total = m_Input.Stashes->size();

					for (std::size_t index = 0; index < total; ++index)
					{
						if (StashMatches((*m_Input.Stashes)[index]))
						{
							shown.push_back(index);
						}
					}
				}

				if (m_Searching && shown.empty())
				{
					return;
				}

				const bool open = AddSection(SidebarSection::Stashes, "stashes", "Stashes", shown.size(), total);
				m_Rows.back().Counted = m_Input.Stashes != nullptr;
				m_Rows.back().CountFailed = m_Input.Stashes == nullptr && m_Input.StashesError != nullptr;

				if (!open)
				{
					return;
				}

				if (m_Input.StashesError != nullptr)
				{
					AddNotice(SidebarSection::Stashes, m_Input.StashesError->Message, kNothingOnItsWay);
				}
				else if (m_Input.Stashes == nullptr)
				{
					AddNotice(SidebarSection::Stashes, "Loading stashes", kOnItsWay);
				}
				else if (m_Input.Stashes->empty())
				{
					AddNotice(SidebarSection::Stashes, "No stashes", kNothingOnItsWay);
				}

				for (const std::size_t index : shown)
				{
					SidebarRow row;
					row.Kind = SidebarRowKind::Stash;
					row.Section = SidebarSection::Stashes;
					row.Stash = index;
					m_Rows.push_back(std::move(row));
				}
			}

			void AddNotice(SidebarSection section, std::string text, bool waiting)
			{
				SidebarRow row;
				row.Kind = SidebarRowKind::Notice;
				row.Section = section;
				row.Label = std::move(text);
				row.Waiting = waiting;
				m_Rows.push_back(std::move(row));
			}

			bool StashMatches(const Core::StashEntry& stash) const
			{
				for (const std::string& word : m_Words)
				{
					if (!Core::LoweredTextContains(stash.Message, word))
					{
						return false;
					}
				}

				return true;
			}

			const SidebarInput& m_Input;
			BranchLines m_Lines;
			std::vector<std::string> m_Words;
			bool m_Searching;
			std::optional<std::size_t> m_Shown; // into the read's refs

			std::vector<bool> m_IsBase;
			std::vector<std::optional<std::size_t>> m_LocalCopyOf; // per ref: the local branch that is one row with it
			std::vector<bool> m_BaseOwnsRow;                       // per ref: a local base is one row with this remote branch
			std::vector<SidebarRow> m_Rows;
		};
	}

	std::vector<SidebarRow> BuildSidebarRows(const SidebarInput& input)
	{
		return Builder{input}.Build();
	}

	std::optional<std::size_t> RefOpenedBy(const SidebarRow& row, const LineRead& read, const BranchLines& lines)
	{
		if (row.Kind != SidebarRowKind::Branch && row.Kind != SidebarRowKind::Base)
		{
			return std::nullopt;
		}

		if (row.Kind == SidebarRowKind::Base && row.LocalCopy.has_value())
		{
			const std::size_t copy = *row.LocalCopy;
			const bool checkedOut = read.Branches && copy < read.Branches->size() && (*read.Branches)[copy].IsHead;
			const Core::BranchLine* const line = lines.LineOf(read.Refs[copy].FullName);
			const bool hasOwnCommits = line != nullptr && line->State == Core::BranchState::NotMerged;

			if (checkedOut || hasOwnCommits)
			{
				return copy;
			}
		}

		return row.Ref;
	}

	std::optional<std::size_t> RefOfRowMenu(const SidebarRow& row)
	{
		if (row.Kind != SidebarRowKind::Branch && row.Kind != SidebarRowKind::Base)
		{
			return std::nullopt;
		}

		if (row.Kind == SidebarRowKind::Base && row.LocalCopy.has_value())
		{
			return row.LocalCopy;
		}

		return row.Ref;
	}

	std::vector<SidebarRow> BuildStashRows(const std::vector<Core::StashEntry>* stashes, const Core::Error* stashesError,
										   std::string_view search, const std::map<std::string, bool>& openByUser)
	{
		static const LineRead kNoLines;
		static const std::set<std::string> kNoneShowingMerged;

		const SidebarInput input{.Read = kNoLines,
								 .Stashes = stashes,
								 .Authors = nullptr,
								 .Search = search,
								 .OpenByUser = openByUser,
								 .ShowingMerged = kNoneShowingMerged,
								 .StashesError = stashesError};
		return Builder{input}.BuildStashes();
	}
}
