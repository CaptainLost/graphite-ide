#include "Git/Lines/LinesModel.h"

#include "Core/Git/Lines/AncestryGraph.h"
#include "Workspace/Workspace.h"

#include <algorithm>
#include <functional>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kGoldenRatioSeed = 0x9e3779b97f4a7c15ull;
		constexpr std::size_t kSeedLeftShift = 6;
		constexpr std::size_t kSeedRightShift = 2;

		std::size_t CombineHashes(std::size_t seed, std::size_t hash)
		{
			return seed ^ (hash + kGoldenRatioSeed + (seed << kSeedLeftShift) + (seed >> kSeedRightShift));
		}

		// The base tips in the order of the model's bases, so that flags asked for one set of bases never answer another.
		std::vector<Core::Oid> BaseTipsOf(const LineRead& read)
		{
			std::vector<Core::Oid> tips;
			tips.reserve(read.Model.Bases.size());

			for (const Core::LineBase& base : read.Model.Bases)
			{
				tips.push_back(read.Refs[base.Ref].Tip);
			}

			return tips;
		}

		bool IsInGraph(const Core::AncestryGraph& graph, const Core::Oid& commit)
		{
			return graph.Find(commit) != Core::AncestryGraph::kNoCommit;
		}

		bool AreAllInGraph(const Core::AncestryGraph& graph, const std::vector<Core::Oid>& commits)
		{
			for (const Core::Oid& commit : commits)
			{
				if (!IsInGraph(graph, commit))
				{
					return false;
				}
			}

			return true;
		}

		std::vector<Core::AncestryGraph::Index> IndicesOf(const Core::AncestryGraph& graph, const std::vector<Core::Oid>& commits)
		{
			std::vector<Core::AncestryGraph::Index> indices;
			indices.reserve(commits.size());

			for (const Core::Oid& commit : commits)
			{
				indices.push_back(graph.Find(commit));
			}

			return indices;
		}
	}

	std::size_t LinesModel::DistanceKeyHash::operator()(const DistanceKey& key) const noexcept
	{
		const std::hash<Core::Oid> hashOid;
		return CombineHashes(hashOid(key.From), hashOid(key.To));
	}

	std::size_t LinesModel::HoldingKeyHash::operator()(const HoldingKey& key) const noexcept
	{
		const std::hash<Core::Oid> hashOid;
		std::size_t seed = hashOid(key.Commit);

		for (const Core::Oid& tip : key.BaseTips)
		{
			seed = CombineHashes(seed, hashOid(tip));
		}

		return seed;
	}

	LinesModel::LinesModel(LinesPort& port, Core::JobSubmitter submit, std::function<std::int64_t()> now)
		: m_Port(port), m_Submit(std::move(submit)), m_Now(std::move(now))
	{
	}

	void LinesModel::Deliver(DeliveryInbox::Delivery delivery)
	{
		m_Inbox.Post(std::move(delivery));
	}

	bool LinesModel::IsSameFolder(std::uint64_t folder) const
	{
		return folder == m_Folder.load();
	}

	void LinesModel::OnUpdate()
	{
		m_Inbox.Drain();
	}

	void LinesModel::SubscribeTo(Core::EventBus& events)
	{
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent&) { Clear(); });
		events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) { Clear(); });
	}

	void LinesModel::Clear()
	{
		++m_Folder;

		m_Read.Clear();
		m_ChangeGeneration = 0;
		m_ChangeFailure.reset();
		m_LastAsked.reset();
		m_WaitingBranches.reset();
		m_WaitingChanges.clear();
		m_Distances.Clear();
		m_Holdings.Clear();
	}

	Core::Ref<const LineRead> LinesModel::GetRead() const
	{
		return m_Read.Get();
	}

	const std::optional<Core::Error>& LinesModel::GetError() const
	{
		return m_Read.GetError();
	}

	bool LinesModel::IsReading() const
	{
		return m_Read.IsInFlight();
	}

	std::optional<Core::Error> LinesModel::TakeChangeFailure()
	{
		return std::exchange(m_ChangeFailure, std::nullopt);
	}

	void LinesModel::Follow(const Core::Ref<const std::vector<Core::Branch>>& branches, std::uint64_t listing)
	{
		const bool listedAgain = listing != m_LastListing;
		m_LastListing = listing;

		if (branches == nullptr)
		{
			return;
		}

		// A failed read is asked again with the next listing, so a quiet repository whose listing stays the same is not left
		// a read behind for good.
		const bool retriesFailure = listedAgain && m_Read.GetError().has_value() && !m_Read.IsInFlight();

		if (branches == m_LastAsked && !retriesFailure)
		{
			return;
		}

		m_LastAsked = branches;
		m_WaitingBranches = branches;
		RunNextWaiting();
	}

	void LinesModel::Change(LineAnswerChange change)
	{
		m_WaitingChanges.push_back(std::move(change));
		RunNextWaiting();
	}

	void LinesModel::RunNextWaiting()
	{
		if (m_Read.IsInFlight())
		{
			return;
		}

		// WARNING: A change needs a read to be recomputed on, so changes asked before the first read lands wait for it.
		const Core::Ref<const LineRead> last = m_Read.Get();

		if (!m_WaitingChanges.empty() && last != nullptr)
		{
			StartChange(last);
		}
		else if (m_WaitingBranches != nullptr)
		{
			StartRead(last);
		}
	}

	void LinesModel::StartRead(const Core::Ref<const LineRead>& last)
	{
		LineReadInput input;
		input.Branches = std::exchange(m_WaitingBranches, nullptr);
		input.Now = m_Now();

		if (last != nullptr)
		{
			input.Graph = last->Graph;
			input.Generations = last->Generations;
		}

		const std::uint64_t generation = m_Read.Begin();
		const std::uint64_t folder = m_Folder.load();

		m_Submit([this, generation, folder, input = std::move(input)]() {
			if (!IsSameFolder(folder))
			{
				return;
			}

			Core::Result<LineRead> result = m_Port.ReadLines(input);

			Deliver([this, generation, result = std::move(result)]() mutable { Land(generation, std::move(result)); });
		});
	}

	void LinesModel::StartChange(const Core::Ref<const LineRead>& last)
	{
		LineAnswerChange change = std::move(m_WaitingChanges.front());
		m_WaitingChanges.pop_front();

		const std::uint64_t generation = m_Read.Begin();
		const std::uint64_t folder = m_Folder.load();
		const std::int64_t now = m_Now();
		m_ChangeGeneration = generation;

		m_Submit([this, generation, folder, last, change = std::move(change), now]() {
			if (!IsSameFolder(folder))
			{
				return;
			}

			Core::Result<LineRead> result = m_Port.RecomputeLines(*last, change, now);

			Deliver([this, generation, result = std::move(result)]() mutable { Land(generation, std::move(result)); });
		});
	}

	void LinesModel::Land(std::uint64_t generation, Core::Result<LineRead> result)
	{
		const SlotLanding landing = m_Read.Land(generation, std::move(result));

		if (landing == SlotLanding::Stale)
		{
			return;
		}

		if (landing == SlotLanding::Failed && generation == m_ChangeGeneration)
		{
			m_ChangeFailure = m_Read.GetError();
		}

		RunNextWaiting();
	}

	bool LinesModel::HoldsCommit(const Core::Oid& commit) const
	{
		const Core::Ref<const LineRead> read = m_Read.Get();

		return read != nullptr && IsInGraph(*read->Graph, commit);
	}

	std::optional<Core::LineDistance> LinesModel::DistanceOf(const Core::Oid& from, const Core::Oid& to)
	{
		const Core::Ref<const LineRead> read = m_Read.Get();

		if (read == nullptr)
		{
			return std::nullopt;
		}

		const DistanceKey key{from, to};

		if (const Core::Ref<const Core::LineDistance> held = m_Distances.Get(key); held != nullptr)
		{
			return *held;
		}

		const Core::AncestryGraph::Index fromIndex = read->Graph->Find(from);
		const Core::AncestryGraph::Index toIndex = read->Graph->Find(to);

		if (fromIndex == Core::AncestryGraph::kNoCommit || toIndex == Core::AncestryGraph::kNoCommit)
		{
			return std::nullopt;
		}

		const std::optional<std::uint64_t> generation = m_Distances.Request(key);

		if (!generation.has_value())
		{
			return std::nullopt;
		}

		m_Submit([this, key, generation = *generation, folder = m_Folder.load(), graph = read->Graph, generations = read->Generations,
				  fromIndex, toIndex]() {
			if (!IsSameFolder(folder))
			{
				return;
			}

			const Core::LineDistance distance = Core::MeasureDistance(*graph, *generations, fromIndex, toIndex);

			Deliver([this, key, generation, folder, distance]() {
				if (IsSameFolder(folder))
				{
					m_Distances.Succeed(key, generation, Core::CreateRef<const Core::LineDistance>(distance));
				}
			});
		});

		return std::nullopt;
	}

	std::optional<std::vector<bool>> LinesModel::BasesHolding(const Core::Oid& commit)
	{
		const Core::Ref<const LineRead> read = m_Read.Get();

		if (read == nullptr)
		{
			return std::nullopt;
		}

		HoldingKey key{commit, BaseTipsOf(*read)};

		if (const Core::Ref<const std::vector<bool>> held = m_Holdings.Get(key); held != nullptr)
		{
			return *held;
		}

		const Core::AncestryGraph::Index commitIndex = read->Graph->Find(commit);

		if (commitIndex == Core::AncestryGraph::kNoCommit || !AreAllInGraph(*read->Graph, key.BaseTips))
		{
			return std::nullopt;
		}

		const std::optional<std::uint64_t> generation = m_Holdings.Request(key);

		if (!generation.has_value())
		{
			return std::nullopt;
		}

		std::vector<Core::AncestryGraph::Index> tips = IndicesOf(*read->Graph, key.BaseTips);

		m_Submit([this, key = std::move(key), generation = *generation, folder = m_Folder.load(), graph = read->Graph,
				  generations = read->Generations, commitIndex, tips = std::move(tips)]() {
			if (!IsSameFolder(folder))
			{
				return;
			}

			std::vector<bool> holding = Core::TipsHolding(*graph, *generations, commitIndex, tips);

			Deliver([this, key, generation, folder, holding = std::move(holding)]() mutable {
				if (IsSameFolder(folder))
				{
					m_Holdings.Succeed(key, generation, Core::CreateRef<const std::vector<bool>>(std::move(holding)));
				}
			});
		});

		return std::nullopt;
	}
}
