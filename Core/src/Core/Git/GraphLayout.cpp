#include "corepch.h"

#include "Core/Git/GraphLayout.h"

#include <optional>
#include <unordered_map>

namespace Core {

	namespace {

		// What each lane is waiting for. An empty slot is a lane that is free to be taken
		// by the next commit that needs one.
		using Lanes = std::vector<std::optional<Oid>>;

		std::size_t TakeLaneFor(Lanes& lanes, const Oid& id)
		{
			for (std::size_t lane = 0; lane < lanes.size(); ++lane)
			{
				if (lanes[lane].has_value() && *lanes[lane] == id)
				{
					return lane;
				}
			}

			for (std::size_t lane = 0; lane < lanes.size(); ++lane)
			{
				if (!lanes[lane].has_value())
				{
					lanes[lane] = id;
					return lane;
				}
			}

			lanes.push_back(id);
			return lanes.size() - 1;
		}

		void ClearLane(Lanes& lanes, const Oid& id)
		{
			for (std::optional<Oid>& slot : lanes)
			{
				if (slot.has_value() && *slot == id)
				{
					slot.reset();
				}
			}
		}
	}

	CommitGraph LayoutCommits(const std::vector<CommitSummary>& commits)
	{
		CommitGraph graph;
		graph.Nodes.reserve(commits.size());

		std::unordered_map<Oid, std::size_t> rowOf;
		rowOf.reserve(commits.size());

		for (std::size_t row = 0; row < commits.size(); ++row)
		{
			rowOf.emplace(commits[row].Id, row);
		}

		Lanes lanes;

		// Recorded while walking and resolved to rows afterwards, because a parent's row
		// is not known until the walk reaches it.
		struct PendingEdge
		{
			std::size_t FromRow = 0;
			std::size_t FromLane = 0;
			Oid ToId;
			std::size_t ToLane = 0;
		};

		std::vector<PendingEdge> pending;

		for (std::size_t row = 0; row < commits.size(); ++row)
		{
			const CommitSummary& commit = commits[row];

			const std::size_t lane = TakeLaneFor(lanes, commit.Id);

			GraphNode node;
			node.Id = commit.Id;
			node.Row = row;
			node.Lane = lane;
			graph.Nodes.push_back(node);

			// This commit has been drawn, so nothing is waiting for it any more. Its
			// parents claim lanes below.
			ClearLane(lanes, commit.Id);

			for (std::size_t index = 0; index < commit.Parents.size(); ++index)
			{
				const Oid& parent = commit.Parents[index];

				// A parent outside the loaded page never arrives to release its own
				// reservation, so reserving a lane for it would leak that lane for the
				// rest of the layout. Skip both the reservation and the edge.
				if (rowOf.find(parent) == rowOf.end())
				{
					continue;
				}

				std::size_t parentLane = 0;

				if (index == 0)
				{
					// The first parent continues this commit's line, so it keeps the lane
					// unless another branch already reserved that lane for it.
					bool reserved = false;

					for (std::size_t candidate = 0; candidate < lanes.size(); ++candidate)
					{
						if (lanes[candidate].has_value() && *lanes[candidate] == parent)
						{
							parentLane = candidate;
							reserved = true;
							break;
						}
					}

					if (!reserved)
					{
						// TakeLaneFor always returns an index inside lanes, and lanes
						// never shrinks, so `lane` is guaranteed to be a valid slot here.
						lanes[lane] = parent;
						parentLane = lane;
					}
				}
				else
				{
					parentLane = TakeLaneFor(lanes, parent);
				}

				pending.push_back(PendingEdge{row, lane, parent, parentLane});
			}
		}

		graph.LaneCount = lanes.size();

		for (const PendingEdge& edge : pending)
		{
			const auto target = rowOf.find(edge.ToId);

			if (target == rowOf.end())
			{
				continue;
			}

			GraphEdge line;
			line.FromRow = edge.FromRow;
			line.FromLane = edge.FromLane;
			line.ToRow = target->second;
			line.ToLane = edge.ToLane;
			graph.Edges.push_back(line);
		}

		return graph;
	}
}
