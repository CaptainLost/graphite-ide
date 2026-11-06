#include "corepch.h"

#include "Core/Git/Lines/AncestryGraph.h"

#include <utility>

namespace Core {

	std::uint32_t AncestryGraph::InternAuthor(const std::string& key)
	{
		const auto found = m_authorByKey.find(key);

		if (found != m_authorByKey.end())
		{
			return found->second;
		}

		const auto index = static_cast<std::uint32_t>(m_authorKeys.size());
		m_authorKeys.push_back(key);
		m_authorByKey.emplace(key, index);
		return index;
	}

	void AncestryGraph::Append(std::vector<GraphCommit> commits)
	{
		const std::size_t firstNew = m_ids.size();
		std::vector<std::vector<Oid>> rawParents;

		for (GraphCommit& commit : commits)
		{
			const auto index = static_cast<Index>(m_ids.size());

			if (!m_indexById.emplace(commit.Id, index).second)
			{
				continue;
			}

			m_ids.push_back(commit.Id);
			m_times.push_back(commit.Time);
			m_authors.push_back(InternAuthor(commit.AuthorKey));
			m_parentsCut.push_back(commit.ParentsCut);
			m_committedByAuthor.push_back(commit.CommittedByAuthor);
			rawParents.push_back(std::move(commit.Parents));
		}

		// Every commit of the batch is indexed, so a parent appended after its child resolves here.
		for (std::size_t i = 0; i < rawParents.size(); ++i)
		{
			// Without its first parent a commit keeps none: a second parent moved into the first place would be
			// walked as the line the commit was made on.
			const bool firstParentMissing = !rawParents[i].empty() && Find(rawParents[i].front()) == kNoCommit;

			for (const Oid& parent : rawParents[i])
			{
				const Index found = firstParentMissing ? kNoCommit : Find(parent);

				if (found == kNoCommit)
				{
					m_parentsCut[firstNew + i] = true;
				}
				else
				{
					m_parents.push_back(found);
				}
			}

			m_parentStarts.push_back(m_parents.size());
		}
	}

	std::size_t AncestryGraph::Size() const
	{
		return m_ids.size();
	}

	AncestryGraph::Index AncestryGraph::Find(const Oid& id) const
	{
		const auto found = m_indexById.find(id);
		return found == m_indexById.end() ? kNoCommit : found->second;
	}

	const Oid& AncestryGraph::IdOf(Index commit) const
	{
		return m_ids[commit];
	}

	std::span<const AncestryGraph::Index> AncestryGraph::ParentsOf(Index commit) const
	{
		const std::size_t begin = m_parentStarts[commit];
		return std::span<const Index>(m_parents).subspan(begin, m_parentStarts[commit + 1] - begin);
	}

	AncestryGraph::Index AncestryGraph::FirstParentOf(Index commit) const
	{
		const std::span<const Index> parents = ParentsOf(commit);
		return parents.empty() ? kNoCommit : parents.front();
	}

	std::int64_t AncestryGraph::TimeOf(Index commit) const
	{
		return m_times[commit];
	}

	std::uint32_t AncestryGraph::AuthorOf(Index commit) const
	{
		return m_authors[commit];
	}

	const std::vector<std::string>& AncestryGraph::AuthorKeys() const
	{
		return m_authorKeys;
	}

	bool AncestryGraph::ParentsCutAt(Index commit) const
	{
		return m_parentsCut[commit];
	}

	bool AncestryGraph::CommittedByAuthor(Index commit) const
	{
		return m_committedByAuthor[commit];
	}

	std::vector<GraphCommit> AncestryGraph::ToCommits() const
	{
		std::vector<GraphCommit> commits;
		commits.reserve(m_ids.size());

		for (Index i = 0; i < m_ids.size(); ++i)
		{
			GraphCommit commit;
			commit.Id = m_ids[i];
			commit.Time = m_times[i];
			commit.AuthorKey = m_authorKeys[m_authors[i]];
			commit.ParentsCut = m_parentsCut[i];
			commit.CommittedByAuthor = m_committedByAuthor[i];

			for (const Index parent : ParentsOf(i))
			{
				commit.Parents.push_back(m_ids[parent]);
			}

			commits.push_back(std::move(commit));
		}

		return commits;
	}
}
