#include "Git/Tags/FirstTag.h"

#include "Core/Git/Lines/LineQuestions.h"

#include <cstddef>

namespace Graphite {

	namespace {
		// Indices into `tagsNewestFirst`, oldest first, two of one date by name. The list is newest first by date and,
		// within a date, by name, so it is read from the end one run of a date at a time, each run front to back.
		std::vector<std::size_t> OldestFirst(const std::vector<Core::Tag>& tagsNewestFirst)
		{
			std::vector<std::size_t> order;
			order.reserve(tagsNewestFirst.size());

			std::size_t runEnd = tagsNewestFirst.size();

			while (runEnd > 0)
			{
				std::size_t runStart = runEnd - 1;

				while (runStart > 0 && tagsNewestFirst[runStart - 1].When == tagsNewestFirst[runEnd - 1].When)
				{
					--runStart;
				}

				for (std::size_t index = runStart; index < runEnd; ++index)
				{
					order.push_back(index);
				}

				runEnd = runStart;
			}

			return order;
		}
	}

	std::vector<std::uint32_t> FirstTagsIn(const Core::AncestryGraph& graph, const std::vector<Core::Tag>& tagsNewestFirst)
	{
		const std::vector<std::size_t> order = OldestFirst(tagsNewestFirst);

		// A tag whose commit the graph lacks is kNoCommit here, which the pass passes over.
		std::vector<Core::AncestryGraph::Index> commitsOldestFirst;
		commitsOldestFirst.reserve(order.size());

		for (const std::size_t tag : order)
		{
			commitsOldestFirst.push_back(graph.Find(tagsNewestFirst[tag].Target));
		}

		std::vector<std::uint32_t> firstTags = Core::OldestTagHolding(graph, commitsOldestFirst);

		// From a place in the oldest-first order to the tag's index in the listing.
		for (std::uint32_t& firstTag : firstTags)
		{
			if (firstTag != Core::kNoTag)
			{
				firstTag = static_cast<std::uint32_t>(order[firstTag]);
			}
		}

		return firstTags;
	}

	const Core::Tag* FirstTagOf(const LineRead& read, const Core::Oid& commit)
	{
		if (read.Graph == nullptr || read.Tags == nullptr)
		{
			return nullptr;
		}

		const Core::AncestryGraph::Index index = read.Graph->Find(commit);

		if (index == Core::AncestryGraph::kNoCommit || index >= read.FirstTag.size())
		{
			return nullptr;
		}

		const std::uint32_t firstTag = read.FirstTag[index];

		if (firstTag == Core::kNoTag || firstTag >= read.Tags->size())
		{
			return nullptr;
		}

		return &(*read.Tags)[firstTag];
	}

	bool RepositoryHasTags(const LineRead* read, const Core::Ref<const std::vector<Core::Tag>>& listed)
	{
		if (listed != nullptr)
		{
			return !listed->empty();
		}

		return read != nullptr && read->Tags != nullptr && !read->Tags->empty();
	}
}
