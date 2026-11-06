#include "Git/Content/ContentModel.h"

#include "Git/Content/ContentOrder.h"
#include "Workspace/Workspace.h"

#include "Core/Base/Log.h"
#include "Core/Git/Lines/LineQuestions.h"

#include <span>
#include <utility>

namespace Graphite {

	namespace {

		// A folder switch refuses what was asked before it, and its answer is dropped unseen.
		bool IsWorthLogging(const Core::Error& error)
		{
			return error.Code != Core::ErrorCode::Cancelled;
		}

		void LogFailedCheck(const ContentKey& key, const Core::Error& error)
		{
			if (IsWorthLogging(error))
			{
				APP_WARN("Could not check whether {} is merged into {} by its contents: {}", key.BranchTip.ToHex(),
						 key.LineTip.ToHex(), error.Message);
			}
		}

		// NotFound is a missing commit or object, as in a blobless clone: an answer, not a failure.
		void LogFailedTwinSearch(const Core::Oid& commit, const Core::Oid& lineTip, const Core::Error& error)
		{
			if (IsWorthLogging(error) && error.Code != Core::ErrorCode::NotFound)
			{
				APP_WARN("Could not search {} for a copy of {}: {}", lineTip.ToHex(), commit.ToHex(), error.Message);
			}
		}

		ContentMark KnownMark(const Core::ContentVerdict& verdict)
		{
			return ContentMark{.State = ContentMark::State::Known, .Verdict = verdict};
		}

		// A read without a graph holds nothing; one handed on without generations gets them computed.
		std::vector<RememberedVerdict> HeldIn(const Core::Ref<const Core::AncestryGraph>& graph,
											  const Core::Ref<const std::vector<std::uint32_t>>& generations,
											  const std::vector<RememberedVerdict>& remembered)
		{
			if (graph == nullptr)
			{
				return {};
			}

			if (generations != nullptr)
			{
				return VerdictsStillHeld(*graph, *generations, remembered);
			}

			const std::vector<std::uint32_t> computed = Core::GenerationsOf(*graph);
			return VerdictsStillHeld(*graph, computed, remembered);
		}
	}

	ContentModel::ContentModel(ContentPort& port, Core::JobSubmitter submit) : m_Port(port), m_Submit(std::move(submit))
	{
	}

	void ContentModel::Deliver(DeliveryInbox::Delivery delivery)
	{
		m_Inbox.Post(std::move(delivery));
	}

	bool ContentModel::IsSameFolder(std::uint64_t folder) const
	{
		return folder == m_Folder.load();
	}

	void ContentModel::SubscribeTo(Core::EventBus& events)
	{
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent&) { Clear(); });
		events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) { Clear(); });
	}

	void ContentModel::OnUpdate(Core::Timestep timestep)
	{
		m_Clock += timestep.GetSeconds();
		m_Inbox.Drain();

		const std::set<std::size_t> inView = std::exchange(m_InView, {});
		const std::optional<std::size_t> shown = std::exchange(m_Shown, std::nullopt);
		const std::vector<TwinKey> twinsAsked = std::exchange(m_TwinsAsked, {});

		StartNext(inView, shown, twinsAsked);
		SaveWhenDue();
	}

	void ContentModel::SetOnScreen(bool onScreen)
	{
		m_OnScreen = onScreen;
	}

	void ContentModel::Clear()
	{
		SaveChanges();

		++m_Folder;

		m_Read.reset();
		m_BranchOfRef.clear();
		m_Store.reset();
		m_Cache = ContentCache{};
		m_PatchIds.reset();

		m_Running = false;
		m_Checking.reset();
		m_TwinChecking.reset();

		m_Unknown.clear();
		m_RecheckWaiting.clear();
		m_RecheckRunning.clear();
		m_EveryCandidateAsked = false;
		m_Twins.clear();

		m_InView.clear();
		m_Shown.reset();
		m_TwinsAsked.clear();
		m_Sources.clear();

		m_VerdictsChanged = false;
		m_PatchIdsChanged = false;
	}

	void ContentModel::Follow(Core::Ref<const LineRead> read)
	{
		if (read == m_Read)
		{
			return;
		}

		const bool storeChanged = read != nullptr && m_Store.has_value() && m_Store->Content != read->Store.Content;

		if (storeChanged)
		{
			Clear();
		}

		if (read != nullptr && !m_Store.has_value())
		{
			LoadStore(read->Store);
		}

		m_Read = std::move(read);

		// What was asked in the last frame indexes the read before.
		m_InView.clear();
		m_Shown.reset();

		m_EveryCandidateAsked = false;
		IndexBranches();
		FindVerdictsToRecheck();
		RebuildSources();
	}

	void ContentModel::LoadStore(const LineStorePaths& store)
	{
		m_Store = store;
		m_Cache = LoadContentCache(store.Content);
		m_PatchIds = Core::CreateRef<PatchIdStore>();
		m_PatchIds->File = store.Patches;
		m_LastSaveAt = m_Clock;
	}

	void ContentModel::IndexBranches()
	{
		m_BranchOfRef.clear();

		if (m_Read == nullptr)
		{
			return;
		}

		m_BranchOfRef.assign(m_Read->Refs.size(), std::nullopt);

		for (std::size_t branch = 0; branch < m_Read->Model.Branches.size(); ++branch)
		{
			const std::size_t ref = m_Read->Model.Branches[branch].Ref;

			if (ref < m_BranchOfRef.size())
			{
				m_BranchOfRef[ref] = branch;
			}
		}
	}

	void ContentModel::FindVerdictsToRecheck()
	{
		m_RecheckWaiting.clear();

		if (m_Read == nullptr)
		{
			return;
		}

		for (const Core::BranchLine& line : m_Read->Model.Branches)
		{
			const std::optional<ContentKey> key = KeyOf(line);

			if (IsContentCandidate(line) && key.has_value() && IsWaiting(*key) && m_Cache.Found.contains(key->BranchTip))
			{
				m_RecheckWaiting.insert(*key);
			}
		}
	}

	void ContentModel::RebuildSources()
	{
		m_Sources.clear();

		if (m_Read == nullptr)
		{
			return;
		}

		for (const std::size_t branch : OrderCandidates(*m_Read, {}, std::nullopt))
		{
			const Core::BranchLine& line = m_Read->Model.Branches[branch];
			const std::optional<ContentKey> key = KeyOf(line);

			if (!key.has_value())
			{
				continue;
			}

			const std::map<ContentKey, Core::ContentVerdict>::const_iterator verdict = m_Cache.Verdicts.find(*key);

			if (verdict != m_Cache.Verdicts.end() && verdict->second.Outcome == Core::ContentOutcome::Merged)
			{
				AddSourcesOf(m_Read->Refs[line.Ref].FullName, verdict->second);
			}
		}
	}

	void ContentModel::AddSourcesOf(const std::string& branch, const Core::ContentVerdict& verdict)
	{
		if (verdict.Kind == Core::ContentKind::Squash && verdict.Landing.has_value())
		{
			m_Sources.emplace(*verdict.Landing, ContentSource{.Branch = branch, .Kind = Core::ContentKind::Squash, .Own = std::nullopt});
		}

		for (const Core::PatchTwin& twin : verdict.Twins)
		{
			m_Sources.emplace(twin.Twin, ContentSource{.Branch = branch, .Kind = Core::ContentKind::Rebase, .Own = twin.Own});
		}
	}

	std::optional<std::size_t> ContentModel::BranchIndexOf(const Core::BranchLine& line) const
	{
		if (line.Ref >= m_BranchOfRef.size())
		{
			return std::nullopt;
		}

		return m_BranchOfRef[line.Ref];
	}

	std::optional<ContentKey> ContentModel::KeyOf(const Core::BranchLine& line) const
	{
		if (m_Read == nullptr)
		{
			return std::nullopt;
		}

		return ContentKeyOf(*m_Read, line);
	}

	bool ContentModel::IsWaiting(const ContentKey& key) const
	{
		const bool answered = m_Cache.Verdicts.contains(key) || m_Unknown.contains(key);
		const bool rechecking = m_RecheckWaiting.contains(key) || m_RecheckRunning.contains(key);

		return !answered && !rechecking && m_Checking != key;
	}

	bool ContentModel::IsWaitingBranch(std::size_t branch) const
	{
		if (m_Read == nullptr || branch >= m_Read->Model.Branches.size())
		{
			return false;
		}

		const Core::BranchLine& line = m_Read->Model.Branches[branch];
		const std::optional<ContentKey> key = KeyOf(line);

		return IsContentCandidate(line) && key.has_value() && IsWaiting(*key);
	}

	std::optional<std::size_t> ContentModel::FirstWaitingBranch(const std::vector<std::size_t>& order) const
	{
		for (const std::size_t branch : order)
		{
			if (IsWaitingBranch(branch))
			{
				return branch;
			}
		}

		return std::nullopt;
	}

	std::optional<ContentModel::TwinKey> ContentModel::FirstTwinWaiting(const std::vector<TwinKey>& asked) const
	{
		for (const TwinKey& key : asked)
		{
			if (!m_Twins.contains(key) && m_TwinChecking != key)
			{
				return key;
			}
		}

		return std::nullopt;
	}

	bool ContentModel::IsTwinAsked(const TwinKey& key) const
	{
		for (const TwinKey& asked : m_TwinsAsked)
		{
			if (asked == key)
			{
				return true;
			}
		}

		return false;
	}

	void ContentModel::StartNext(const std::set<std::size_t>& inView, std::optional<std::size_t> shown,
								 const std::vector<TwinKey>& twinsAsked)
	{
		if (m_Running || !m_OnScreen || m_Read == nullptr)
		{
			return;
		}

		if (!m_RecheckWaiting.empty())
		{
			StartRecheck();
			return;
		}

		if (shown.has_value() && IsWaitingBranch(*shown))
		{
			StartCheck(*shown);
			return;
		}

		if (const std::optional<TwinKey> twin = FirstTwinWaiting(twinsAsked); twin.has_value())
		{
			StartTwin(*twin);
			return;
		}

		if (m_EveryCandidateAsked)
		{
			return;
		}

		const std::optional<std::size_t> next = FirstWaitingBranch(OrderCandidates(*m_Read, inView, shown));

		if (!next.has_value())
		{
			m_EveryCandidateAsked = true;
			return;
		}

		StartCheck(*next);
	}

	void ContentModel::StartRecheck()
	{
		std::vector<RememberedVerdict> remembered;
		remembered.reserve(m_RecheckWaiting.size());

		for (const ContentKey& key : m_RecheckWaiting)
		{
			remembered.push_back(RememberedVerdict{.Key = key, .Verdict = m_Cache.Found.at(key.BranchTip).Verdict});
		}

		m_RecheckRunning = std::exchange(m_RecheckWaiting, {});
		m_Running = true;

		m_Submit([this, folder = m_Folder.load(), graph = m_Read->Graph, generations = m_Read->Generations,
				  remembered = std::move(remembered)]() {
			if (!IsSameFolder(folder))
			{
				return;
			}

			std::vector<RememberedVerdict> held = HeldIn(graph, generations, remembered);

			Deliver([this, folder, held = std::move(held)]() {
				if (IsSameFolder(folder))
				{
					LandRecheck(held);
				}
			});
		});
	}

	void ContentModel::LandRecheck(const std::vector<RememberedVerdict>& held)
	{
		m_Running = false;
		m_RecheckRunning.clear();

		// Those the line no longer holds wait for a check of their own.
		m_EveryCandidateAsked = false;

		if (held.empty())
		{
			return;
		}

		for (const RememberedVerdict& remembered : held)
		{
			m_Cache.Verdicts[remembered.Key] = remembered.Verdict;
		}

		m_VerdictsChanged = true;
		RebuildSources();
	}

	void ContentModel::StartCheck(std::size_t branch)
	{
		const Core::BranchLine& line = m_Read->Model.Branches[branch];
		const ContentKey key = *KeyOf(line);
		std::string lineName = m_Read->Refs[*line.ComparedWith].FullName;
		const std::uint64_t repository = m_Port.RepositoryNow();

		m_Checking = key;
		m_Running = true;

		m_Submit([this, key, lineName = std::move(lineName), repository, folder = m_Folder.load(), patchIds = m_PatchIds]() {
			if (!IsSameFolder(folder))
			{
				return;
			}

			LoadPatchIdsOnce(*patchIds);
			const std::size_t idsBefore = patchIds->Ids.GetSize();

			Core::Result<Core::ContentVerdict> verdict = m_Port.CheckContent(repository, key.BranchTip, key.LineTip, patchIds->Ids);
			const bool patchIdsGrew = patchIds->Ids.GetSize() > idsBefore;

			Deliver([this, folder, key, lineName, verdict = std::move(verdict), patchIdsGrew]() {
				if (IsSameFolder(folder))
				{
					LandCheck(key, lineName, verdict, patchIdsGrew);
				}
			});
		});
	}

	void ContentModel::LandCheck(const ContentKey& key, const std::string& lineName, const Core::Result<Core::ContentVerdict>& verdict,
								 bool patchIdsGrew)
	{
		m_Running = false;
		m_Checking.reset();
		m_PatchIdsChanged = m_PatchIdsChanged || patchIdsGrew;

		if (!verdict.HasValue())
		{
			LogFailedCheck(key, verdict.GetError());
			m_Unknown.insert(key);
			return;
		}

		const Core::ContentVerdict& found = verdict.GetValue();

		if (found.Outcome == Core::ContentOutcome::Unknown)
		{
			m_Unknown.insert(key);
			return;
		}

		m_Cache.Verdicts[key] = found;
		m_VerdictsChanged = true;

		if (found.Outcome == Core::ContentOutcome::Merged)
		{
			m_Cache.Found[key.BranchTip] = ContentFound{.Line = lineName, .Verdict = found};
			RebuildSources();
		}
	}

	void ContentModel::StartTwin(const TwinKey& key)
	{
		const std::uint64_t repository = m_Port.RepositoryNow();

		m_TwinChecking = key;
		m_Running = true;

		m_Submit([this, key, repository, folder = m_Folder.load(), patchIds = m_PatchIds]() {
			if (!IsSameFolder(folder))
			{
				return;
			}

			LoadPatchIdsOnce(*patchIds);
			const std::size_t idsBefore = patchIds->Ids.GetSize();

			Core::Result<std::optional<Core::Oid>> twin = m_Port.FindTwin(repository, key.Commit, key.LineTip, patchIds->Ids);
			const bool patchIdsGrew = patchIds->Ids.GetSize() > idsBefore;

			Deliver([this, folder, key, twin = std::move(twin), patchIdsGrew]() {
				if (IsSameFolder(folder))
				{
					LandTwin(key, twin, patchIdsGrew);
				}
			});
		});
	}

	void ContentModel::LandTwin(const TwinKey& key, const Core::Result<std::optional<Core::Oid>>& twin, bool patchIdsGrew)
	{
		m_Running = false;
		m_TwinChecking.reset();
		m_PatchIdsChanged = m_PatchIdsChanged || patchIdsGrew;

		if (twin.HasValue())
		{
			m_Twins[key] = TwinMark{.State = ContentMark::State::Known, .Twin = twin.GetValue()};
			return;
		}

		LogFailedTwinSearch(key.Commit, key.LineTip, twin.GetError());
		m_Twins[key] = TwinMark{.State = ContentMark::State::Unknown, .Twin = std::nullopt};
	}

	ContentMark ContentModel::MarkOf(const Core::BranchLine& line)
	{
		if (const std::optional<std::size_t> branch = BranchIndexOf(line); branch.has_value())
		{
			m_InView.insert(*branch);
		}

		return KnownMarkOf(line);
	}

	ContentMark ContentModel::MarkOfShown(const Core::BranchLine& line)
	{
		if (const std::optional<std::size_t> branch = BranchIndexOf(line); branch.has_value())
		{
			m_Shown = *branch;
		}

		return KnownMarkOf(line);
	}

	ContentMark ContentModel::KnownMarkOf(const Core::BranchLine& line) const
	{
		if (!IsContentCandidate(line))
		{
			return KnownMark(Core::ContentVerdict{});
		}

		const std::optional<ContentKey> key = KeyOf(line);

		if (!key.has_value())
		{
			return ContentMark{};
		}

		const std::map<ContentKey, Core::ContentVerdict>::const_iterator verdict = m_Cache.Verdicts.find(*key);

		if (verdict != m_Cache.Verdicts.end())
		{
			return KnownMark(verdict->second);
		}

		if (m_Unknown.contains(*key))
		{
			return ContentMark{.State = ContentMark::State::Unknown, .Verdict = std::nullopt};
		}

		return ContentMark{};
	}

	std::optional<ContentSource> ContentModel::SourceOf(const Core::Oid& commit) const
	{
		const std::unordered_map<Core::Oid, ContentSource>::const_iterator source = m_Sources.find(commit);

		if (source == m_Sources.end())
		{
			return std::nullopt;
		}

		return source->second;
	}

	TwinMark ContentModel::TwinIn(const Core::Oid& commit, const Core::Oid& lineTip)
	{
		const TwinKey key{.Commit = commit, .LineTip = lineTip};
		const std::map<TwinKey, TwinMark>::const_iterator answered = m_Twins.find(key);

		if (answered != m_Twins.end())
		{
			return answered->second;
		}

		if (m_TwinChecking != key && !IsTwinAsked(key))
		{
			m_TwinsAsked.push_back(key);
		}

		return TwinMark{};
	}

	void ContentModel::SaveWhenDue()
	{
		const bool changed = m_VerdictsChanged || m_PatchIdsChanged;

		if (changed && m_Clock - m_LastSaveAt >= kSecondsBetweenSaves)
		{
			SaveChanges();
		}
	}

	void ContentModel::SaveChanges()
	{
		const bool changed = m_VerdictsChanged || m_PatchIdsChanged;

		if (!changed || !m_Store.has_value() || m_Store->Content.empty())
		{
			return;
		}

		ContentWrite write;
		write.Sequence = ++m_SaveSequence;
		write.VerdictsFile = m_Store->Content;

		if (m_VerdictsChanged)
		{
			write.Verdicts = m_Read != nullptr ? VerdictsUsedBy(*m_Read, m_Cache) : m_Cache;
		}

		if (m_PatchIdsChanged)
		{
			write.PatchIds = m_PatchIds;
		}

		m_VerdictsChanged = false;
		m_PatchIdsChanged = false;
		m_LastSaveAt = m_Clock;

		m_Submit([this, write = std::move(write)]() { m_Writer.Write(write); });
	}
}
