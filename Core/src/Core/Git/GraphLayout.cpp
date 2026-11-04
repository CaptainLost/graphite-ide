#include "corepch.h"

#include "Core/Git/GraphLayout.h"

#include "Core/Base/Assert.h"

#include <algorithm>
#include <optional>
#include <unordered_map>

namespace Core {

	namespace {

		// Each slot holds the commit its lane is waiting for; an empty slot is a free lane.
		using LaneReservations = std::vector<std::optional<Oid>>;

		using RowOfCommit = std::unordered_map<Oid, std::size_t>;

		struct PendingEdge
		{
			std::size_t FromRow = 0;
			std::size_t FromLane = 0;
			Oid ToId;
			std::size_t ToLane = 0;
		};

		std::optional<std::size_t> FindLaneWaitingFor(const LaneReservations& lanes, const Oid& id)
		{
			for (std::size_t lane = 0; lane < lanes.size(); ++lane)
			{
				if (lanes[lane].has_value() && *lanes[lane] == id)
				{
					return lane;
				}
			}

			return std::nullopt;
		}

		std::size_t TakeLaneFor(LaneReservations& lanes, const Oid& id)
		{
			const std::optional<std::size_t> waitingLane = FindLaneWaitingFor(lanes, id);

			if (waitingLane.has_value())
			{
				return *waitingLane;
			}

			for (std::size_t lane = 0; lane < lanes.size(); ++lane)
			{
				const bool laneIsFree = !lanes[lane].has_value();

				if (laneIsFree)
				{
					lanes[lane] = id;
					return lane;
				}
			}

			lanes.push_back(id);
			return lanes.size() - 1;
		}

		void ReleaseLanesWaitingFor(LaneReservations& lanes, const Oid& id)
		{
			for (std::optional<Oid>& reservation : lanes)
			{
				if (reservation.has_value() && *reservation == id)
				{
					reservation.reset();
				}
			}
		}

		std::optional<std::size_t> FindRowOf(const RowOfCommit& rowOfCommit, const Oid& id)
		{
			const RowOfCommit::const_iterator entry = rowOfCommit.find(id);

			if (entry == rowOfCommit.end())
			{
				return std::nullopt;
			}

			const auto& [commitId, row] = *entry;
			return row;
		}
	}

	CommitGraph LayoutCommits(const std::vector<CommitSummary>& commits, LayoutParents parents)
	{
		CommitGraph graph;
		graph.Nodes.reserve(commits.size());

		RowOfCommit rowOfCommit;
		rowOfCommit.reserve(commits.size());

		for (std::size_t row = 0; row < commits.size(); ++row)
		{
			rowOfCommit.emplace(commits[row].Id, row);
		}

		LaneReservations lanes;
		std::vector<PendingEdge> pendingEdges;

		for (std::size_t row = 0; row < commits.size(); ++row)
		{
			const CommitSummary& commit = commits[row];

			const std::size_t lane = TakeLaneFor(lanes, commit.Id);

			graph.Nodes.push_back(GraphNode{.Id = commit.Id, .Row = row, .Lane = lane});

			ReleaseLanesWaitingFor(lanes, commit.Id);

			const std::size_t parentsLaidOut =
				parents == LayoutParents::FirstOnly ? std::min<std::size_t>(commit.Parents.size(), 1) : commit.Parents.size();

			for (std::size_t parentIndex = 0; parentIndex < parentsLaidOut; ++parentIndex)
			{
				const Oid& parent = commit.Parents[parentIndex];
				const bool parentIsOnPage = rowOfCommit.contains(parent);

				if (!parentIsOnPage)
				{
					continue;
				}

				std::size_t parentLane = 0;
				const bool isFirstParent = parentIndex == 0;

				if (isFirstParent)
				{
					const std::optional<std::size_t> reservedLane = FindLaneWaitingFor(lanes, parent);

					if (reservedLane.has_value())
					{
						parentLane = *reservedLane;
					}
					else
					{
						CORE_ASSERT(lane < lanes.size(), "TakeLaneFor returns a slot of lanes, and lanes never shrinks");
						lanes[lane] = parent;
						parentLane = lane;
					}
				}
				else
				{
					parentLane = TakeLaneFor(lanes, parent);
				}

				pendingEdges.push_back(PendingEdge{.FromRow = row, .FromLane = lane, .ToId = parent, .ToLane = parentLane});
			}
		}

		graph.LaneCount = lanes.size();

		for (const PendingEdge& pendingEdge : pendingEdges)
		{
			const std::optional<std::size_t> targetRow = FindRowOf(rowOfCommit, pendingEdge.ToId);

			if (!targetRow.has_value())
			{
				continue;
			}

			graph.Edges.push_back(GraphEdge{
				.FromRow = pendingEdge.FromRow,
				.FromLane = pendingEdge.FromLane,
				.ToRow = *targetRow,
				.ToLane = pendingEdge.ToLane,
			});
		}

		return graph;
	}
}
