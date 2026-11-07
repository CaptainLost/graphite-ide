#include "Git/Content/ContentModel.h"

#include "Git/Content/ContentOrder.h"
#include "Workspace/Workspace.h"

#include "Core/Base/Log.h"

#include <algorithm>
#include <format>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Graphite {

	namespace {

		// The port refuses a question asked for a repository it no longer opens: a folder switch, or the folder moved. It
		// is asked again, of the repository open now.
		bool IsRefused(const Core::Error& error)
		{
			return error.Code == Core::ErrorCode::Cancelled;
		}

		void WarnInLog(const std::string& message)
		{
			APP_WARN("{}", message);
		}

		// Counts a job as unfinished until its body returns, whether it asked anything or found its folder gone.
		class UnfinishedJob : Core::NonCopyable
		{
		public:
			explicit UnfinishedJob(std::atomic<std::size_t>& unfinished) : m_Unfinished(unfinished) {}

			~UnfinishedJob() { --m_Unfinished; }

		private:
			std::atomic<std::size_t>& m_Unfinished;
		};

		ContentMark KnownMark(const Core::ContentVerdict& verdict)
		{
			return ContentMark{.State = ContentMark::State::Known, .Verdict = verdict};
		}

		// A merged verdict is remembered by its branch tip and rechecked against a moved line instead.
		bool CanCarry(const Core::ContentVerdict& verdict)
		{
			return verdict.Outcome != Core::ContentOutcome::Merged;
		}

		// The earlier pairs of the verdicts a moved line has not resolved yet: a session that ends first carries them in the
		// next.
		void KeepEarlierPairsOf(const std::map<ContentKey, CarryAsk>& asks, ContentCache& kept)
		{
			for (const auto& [key, ask] : asks)
			{
				kept.Verdicts[ContentKey{.BranchTip = key.BranchTip, .LineTip = ask.EarlierLineTip}] = ask.Verdict;
			}
		}

		std::unordered_map<std::string_view, std::size_t> BranchesByName(const LineRead& read)
		{
			std::unordered_map<std::string_view, std::size_t> branches;

			for (std::size_t branch = 0; branch < read.Model.Branches.size(); ++branch)
			{
				const std::size_t ref = read.Model.Branches[branch].Ref;

				if (ref < read.Refs.size())
				{
					branches.emplace(read.Refs[ref].FullName, branch);
				}
			}

			return branches;
		}

		// The same branch's place in the next read, by its full name; empty when the next read lacks it.
		std::optional<std::size_t> SameBranchIn(const LineRead& before, std::size_t branch,
												const std::unordered_map<std::string_view, std::size_t>& nextByName)
		{
			if (branch >= before.Model.Branches.size() || before.Model.Branches[branch].Ref >= before.Refs.size())
			{
				return std::nullopt;
			}

			const std::string& name = before.Refs[before.Model.Branches[branch].Ref].FullName;
			const std::unordered_map<std::string_view, std::size_t>::const_iterator found = nextByName.find(name);

			if (found == nextByName.end())
			{
				return std::nullopt;
			}

			return found->second;
		}
	}

	ContentModel::ContentModel(ContentPort& port, Core::JobSubmitter submit)
		: LogFailure(WarnInLog), m_Port(port), m_Submit(std::move(submit))
	{
	}

	void ContentModel::ReportFailure(const std::string& message) const
	{
		if (LogFailure)
		{
			LogFailure(message);
		}
	}

	void ContentModel::Submit(std::function<void()> job)
	{
		++m_UnfinishedJobs;
		m_Submit(std::move(job));
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

		m_Failed.clear();
		m_RecheckWaiting.clear();
		m_RecheckRunning.clear();
		m_CarryWaiting.clear();
		m_CarryRunning.clear();
		m_EveryCandidateAsked = false;
		m_Twins.clear();
		m_TwinsReadSoFar.clear();

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

		// WARNING: The pairs view into the names of the read before, held here until they are used.
		const Core::Ref<const LineRead> before = m_Read;
		const PairsByName pairsBefore = before != nullptr && read != nullptr ? PairsOf(*before) : PairsByName{};

		CarryAsksTo(read);
		m_Read = std::move(read);

		m_EveryCandidateAsked = false;
		IndexBranches();
		FindVerdictsToResolve(pairsBefore);
		RebuildSources();
	}

	ContentModel::PairsByName ContentModel::PairsOf(const LineRead& before) const
	{
		PairsByName pairs;

		for (const Core::BranchLine& line : before.Model.Branches)
		{
			const std::optional<ContentKey> key = ContentKeyOf(before, line);

			if (IsContentCandidate(line) && key.has_value())
			{
				pairs.emplace(before.Refs[line.Ref].FullName, *key);
			}
		}

		return pairs;
	}

	void ContentModel::CarryAsksTo(const Core::Ref<const LineRead>& next)
	{
		const bool asked = m_Shown.has_value() || !m_InView.empty();

		if (m_Read == nullptr || next == nullptr || !asked)
		{
			m_InView.clear();
			m_Shown.reset();
			return;
		}

		const std::unordered_map<std::string_view, std::size_t> nextByName = BranchesByName(*next);
		std::set<std::size_t> inView;

		for (const std::size_t branch : m_InView)
		{
			if (const std::optional<std::size_t> same = SameBranchIn(*m_Read, branch, nextByName); same.has_value())
			{
				inView.insert(*same);
			}
		}

		m_InView = std::move(inView);

		if (m_Shown.has_value())
		{
			m_Shown = SameBranchIn(*m_Read, *m_Shown, nextByName);
		}
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

	void ContentModel::FindVerdictsToResolve(const PairsByName& before)
	{
		m_RecheckWaiting.clear();
		m_CarryWaiting.clear();

		if (m_Read == nullptr)
		{
			return;
		}

		for (const Core::BranchLine& line : m_Read->Model.Branches)
		{
			const std::optional<ContentKey> key = KeyOf(line);

			if (!IsContentCandidate(line) || !key.has_value() || !IsWaiting(*key))
			{
				continue;
			}

			if (m_Cache.Found.contains(key->BranchTip))
			{
				m_RecheckWaiting.insert(*key);
				continue;
			}

			const PairsByName::const_iterator pairBefore = before.find(m_Read->Refs[line.Ref].FullName);
			const std::optional<ContentKey> keyBefore =
				pairBefore != before.end() ? std::optional<ContentKey>{pairBefore->second} : std::nullopt;

			if (std::optional<CarryAsk> ask = EarlierVerdictOf(*key, keyBefore); ask.has_value())
			{
				m_CarryWaiting.emplace(*key, std::move(*ask));
			}
		}
	}

	std::optional<CarryAsk> ContentModel::EarlierVerdictOf(const ContentKey& key, const std::optional<ContentKey>& before) const
	{
		// The read before's pair first: the tip the line had until now.
		if (before.has_value() && before->BranchTip == key.BranchTip && before->LineTip != key.LineTip)
		{
			const std::map<ContentKey, Core::ContentVerdict>::const_iterator verdict = m_Cache.Verdicts.find(*before);

			if (verdict != m_Cache.Verdicts.end() && CanCarry(verdict->second))
			{
				return CarryAsk{.Key = key, .EarlierLineTip = before->LineTip, .Verdict = verdict->second};
			}
		}

		// Else one kept for the branch tip against another line tip, as the file of an earlier session holds it.
		const ContentKey first{.BranchTip = key.BranchTip, .LineTip = Core::Oid{}};

		for (std::map<ContentKey, Core::ContentVerdict>::const_iterator verdict = m_Cache.Verdicts.lower_bound(first);
			 verdict != m_Cache.Verdicts.end() && verdict->first.BranchTip == key.BranchTip; ++verdict)
		{
			if (verdict->first.LineTip != key.LineTip && CanCarry(verdict->second))
			{
				return CarryAsk{.Key = key, .EarlierLineTip = verdict->first.LineTip, .Verdict = verdict->second};
			}
		}

		return std::nullopt;
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

			const Core::ContentVerdict* const verdict = StandingVerdictOf(*key);

			if (verdict != nullptr && verdict->Outcome == Core::ContentOutcome::Merged)
			{
				AddSourcesOf(m_Read->Refs[line.Ref].FullName, *verdict);
			}
		}
	}

	const Core::ContentVerdict* ContentModel::StandingVerdictOf(const ContentKey& key) const
	{
		const std::map<ContentKey, Core::ContentVerdict>::const_iterator own = m_Cache.Verdicts.find(key);

		if (own != m_Cache.Verdicts.end())
		{
			return &own->second;
		}

		// Found against an earlier tip of the line, and rechecked against this one: it counts meanwhile.
		if (m_RecheckWaiting.contains(key) || m_RecheckRunning.contains(key))
		{
			const std::map<Core::Oid, ContentFound>::const_iterator found = m_Cache.Found.find(key.BranchTip);
			return found != m_Cache.Found.end() ? &found->second.Verdict : nullptr;
		}

		for (const std::map<ContentKey, CarryAsk>* carries : {&m_CarryWaiting, &m_CarryRunning})
		{
			const std::map<ContentKey, CarryAsk>::const_iterator carry = carries->find(key);

			if (carry != carries->end())
			{
				return &carry->second.Verdict;
			}
		}

		return nullptr;
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
		const bool answered = m_Cache.Verdicts.contains(key) || m_Failed.contains(key);
		const bool rechecking = m_RecheckWaiting.contains(key) || m_RecheckRunning.contains(key);
		const bool carrying = m_CarryWaiting.contains(key) || m_CarryRunning.contains(key);

		return !answered && !rechecking && !carrying && m_Checking != key;
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

	std::optional<ContentModel::TwinKey> ContentModel::FirstTwinWaiting(const std::vector<TwinKey>& asked, bool started) const
	{
		for (const TwinKey& key : asked)
		{
			if (!m_Twins.contains(key) && m_TwinChecking != key && m_TwinsReadSoFar.contains(key) == started)
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
		// A job of the folder before may still run on a worker after a folder switch freed the slot.
		const bool jobRunsOnWorker = m_UnfinishedJobs.load() > 0;

		if (m_Running || jobRunsOnWorker || !m_OnScreen || m_Read == nullptr)
		{
			return;
		}

		if (!m_RecheckWaiting.empty() || !m_CarryWaiting.empty())
		{
			StartResolve();
			return;
		}

		if (shown.has_value() && IsWaitingBranch(*shown))
		{
			StartCheck(*shown);
			return;
		}

		if (const std::optional<TwinKey> twin = FirstTwinWaiting(twinsAsked, false); twin.has_value())
		{
			StartTwin(*twin);
			return;
		}

		std::optional<std::size_t> next;

		if (!m_EveryCandidateAsked)
		{
			next = FirstWaitingBranch(OrderCandidates(*m_Read, inView, shown));
			m_EveryCandidateAsked = !next.has_value();
		}

		// A search's later slices wait for the rows in view as well as for the shown branch: both are asked again every
		// frame they are drawn, so a check asked since a slice started goes before the next one.
		if (next.has_value() && inView.contains(*next))
		{
			StartCheck(*next);
			return;
		}

		if (const std::optional<TwinKey> twin = FirstTwinWaiting(twinsAsked, true); twin.has_value())
		{
			StartTwin(*twin);
			return;
		}

		if (next.has_value())
		{
			StartCheck(*next);
		}
	}

	void ContentModel::StartResolve()
	{
		std::vector<RememberedVerdict> remembered;
		remembered.reserve(m_RecheckWaiting.size());

		for (const ContentKey& key : m_RecheckWaiting)
		{
			remembered.push_back(RememberedVerdict{.Key = key, .Verdict = m_Cache.Found.at(key.BranchTip).Verdict});
		}

		std::vector<CarryAsk> carries;
		carries.reserve(m_CarryWaiting.size());

		for (const auto& [key, ask] : m_CarryWaiting)
		{
			carries.push_back(ask);
		}

		m_RecheckRunning = std::exchange(m_RecheckWaiting, {});
		m_CarryRunning = std::exchange(m_CarryWaiting, {});
		m_Running = true;

		Submit([this, folder = m_Folder.load(), repository = m_Port.RepositoryNow(), graph = m_Read->Graph,
				generations = m_Read->Generations, remembered = std::move(remembered), carries = std::move(carries)]() {
			const UnfinishedJob finishing{m_UnfinishedJobs};

			if (!IsSameFolder(folder))
			{
				return;
			}

			ResolvedVerdicts resolved = ResolveVerdicts(graph, generations, m_Port, repository, remembered, carries);

			Deliver([this, folder, resolved = std::move(resolved)]() {
				if (IsSameFolder(folder))
				{
					LandResolve(resolved);
				}
			});
		});
	}

	void ContentModel::LandResolve(const ResolvedVerdicts& resolved)
	{
		m_Running = false;
		m_RecheckRunning.clear();
		m_CarryRunning.clear();

		// Those neither held nor carried stand no longer: they wait for a check of their own.
		m_EveryCandidateAsked = false;

		for (const RememberedVerdict& remembered : resolved.Held)
		{
			m_Cache.Verdicts[remembered.Key] = remembered.Verdict;
		}

		for (const CarryAsk& carried : resolved.Carry.Carried)
		{
			m_Cache.Verdicts[carried.Key] = carried.Verdict;
		}

		// Asked again, of the repository open now.
		for (const CarryAsk& refused : resolved.Carry.Refused)
		{
			m_CarryWaiting.emplace(refused.Key, refused);
		}

		m_VerdictsChanged = m_VerdictsChanged || !resolved.Held.empty() || !resolved.Carry.Carried.empty();
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

		Submit([this, key, lineName = std::move(lineName), repository, folder = m_Folder.load(), patchIds = m_PatchIds]() {
			const UnfinishedJob finishing{m_UnfinishedJobs};

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
			if (IsRefused(verdict.GetError()))
			{
				return;
			}

			ReportFailure(std::format("Could not check whether {} is merged into {} by its contents: {}", key.BranchTip.ToHex(),
									  key.LineTip.ToHex(), verdict.GetError().Message));
			m_Failed.insert(key);
			return;
		}

		// An unknown answer is kept and saved like the others: asked again, the same tips would answer the same.
		const Core::ContentVerdict& found = verdict.GetValue();

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

		// The commits earlier slices read come first again, their patch ids from the cache; this slice pays for the rest.
		const std::map<TwinKey, std::size_t>::const_iterator readSoFar = m_TwinsReadSoFar.find(key);
		const std::size_t read = readSoFar != m_TwinsReadSoFar.end() ? readSoFar->second : 0;
		const std::size_t budget = std::min(read + Core::kPatchCommitsPerCopySlice, Core::kPatchCommitsPerCheck);

		m_TwinChecking = key;
		m_Running = true;

		Submit([this, key, budget, repository, folder = m_Folder.load(), patchIds = m_PatchIds]() {
			const UnfinishedJob finishing{m_UnfinishedJobs};

			if (!IsSameFolder(folder))
			{
				return;
			}

			LoadPatchIdsOnce(*patchIds);
			const std::size_t idsBefore = patchIds->Ids.GetSize();

			Core::Result<Core::PatchTwinSearch> twin = m_Port.FindTwin(repository, key.Commit, key.LineTip, budget, patchIds->Ids);
			const bool patchIdsGrew = patchIds->Ids.GetSize() > idsBefore;

			Deliver([this, folder, key, budget, twin = std::move(twin), patchIdsGrew]() {
				if (IsSameFolder(folder))
				{
					LandTwin(key, budget, twin, patchIdsGrew);
				}
			});
		});
	}

	void ContentModel::LandTwin(const TwinKey& key, std::size_t budget, const Core::Result<Core::PatchTwinSearch>& twin,
								bool patchIdsGrew)
	{
		m_Running = false;
		m_TwinChecking.reset();
		m_PatchIdsChanged = m_PatchIdsChanged || patchIdsGrew;

		if (twin.HasValue())
		{
			// Nothing found yet with line commits left within the whole budget: the next slice waits its turn.
			if (twin.GetValue().MoreToRead && budget < Core::kPatchCommitsPerCheck)
			{
				m_TwinsReadSoFar[key] = budget;
				return;
			}

			m_TwinsReadSoFar.erase(key);
			m_Twins[key] = TwinMark{.State = ContentMark::State::Known, .Twin = twin.GetValue().Twin};
			return;
		}

		const Core::Error& error = twin.GetError();

		// A refused slice is asked again with the budget it had, of the repository open now.
		if (IsRefused(error))
		{
			return;
		}

		m_TwinsReadSoFar.erase(key);

		// NotFound is a missing commit or object, as in a blobless clone: an answer, not a failure.
		if (error.Code != Core::ErrorCode::NotFound)
		{
			ReportFailure(std::format("Could not search {} for a copy of {}: {}", key.LineTip.ToHex(), key.Commit.ToHex(), error.Message));
		}

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

		const Core::ContentVerdict* const verdict = StandingVerdictOf(*key);

		if (verdict != nullptr && verdict->Outcome != Core::ContentOutcome::Unknown)
		{
			return KnownMark(*verdict);
		}

		if (verdict != nullptr || m_Failed.contains(*key))
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

		// Recorded while its slice runs too: in the frame that slice lands, the next one is still asked for.
		if (!IsTwinAsked(key))
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
			ContentCache kept = m_Read != nullptr ? VerdictsUsedBy(*m_Read, m_Cache) : m_Cache;
			KeepEarlierPairsOf(m_CarryWaiting, kept);
			KeepEarlierPairsOf(m_CarryRunning, kept);
			write.Verdicts = std::move(kept);
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
