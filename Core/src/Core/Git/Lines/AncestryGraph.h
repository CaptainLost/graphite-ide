#pragma once

#include "Core/Git/Oid.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {

	struct GraphCommit
	{
		Oid Id;
		std::vector<Oid> Parents; // first parent first
		std::int64_t Time = 0;    // committer time, seconds since the epoch
		std::string AuthorKey;    // AuthorKeyOf(author e-mail)
		bool ParentsCut = false;  // a shallow boundary: git records parents this repository does not have

		friend bool operator==(const GraphCommit&, const GraphCommit&) = default;
	};

	// The history the line model walks, held as flat arrays so that a hundred thousand commits stay
	// cheap: a commit is a position, and its parents are positions too.
	class AncestryGraph
	{
	public:
		using Index = std::uint32_t;
		static constexpr Index kNoCommit = std::numeric_limits<Index>::max();

		// Duplicates (same Id) are ignored. After the whole batch is in, a parent the graph still lacks is
		// dropped and its child is marked ParentsCut; a commit whose first parent is lacking drops all of them.
		void Append(std::vector<GraphCommit> commits);

		std::size_t Size() const;

		// kNoCommit when absent.
		Index Find(const Oid& id) const;

		const Oid& IdOf(Index commit) const;
		std::span<const Index> ParentsOf(Index commit) const;
		std::int64_t TimeOf(Index commit) const;

		// An index into AuthorKeys().
		std::uint32_t AuthorOf(Index commit) const;

		const std::vector<std::string>& AuthorKeys() const;
		bool ParentsCutAt(Index commit) const;

		// What Append took, for the cache file.
		std::vector<GraphCommit> ToCommits() const;

	private:
		std::uint32_t InternAuthor(const std::string& key);

		std::vector<Oid> m_ids;
		std::vector<std::int64_t> m_times;
		std::vector<std::uint32_t> m_authors;
		std::vector<bool> m_parentsCut;

		// Commit i's parents are m_parents[m_parentStarts[i] .. m_parentStarts[i + 1]).
		std::vector<std::size_t> m_parentStarts{0};
		std::vector<Index> m_parents;

		std::unordered_map<Oid, Index> m_indexById;
		std::vector<std::string> m_authorKeys;
		std::unordered_map<std::string, std::uint32_t> m_authorByKey;
	};
}
