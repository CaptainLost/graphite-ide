#pragma once

#include "Core/Git/Oid.h"

#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Core {

	// A commit of a branch's own and the commit of another history carrying the same patch, as a
	// cherry-pick or a rebase leaves it.
	struct PatchTwin
	{
		Oid Own;
		Oid Twin;

		friend bool operator==(const PatchTwin&, const PatchTwin&) = default;
	};

	struct PatchMatch
	{
		std::size_t Ahead = 0;          // commits tip has that base lacks, merges included
		std::size_t Behind = 0;         // commits base has that tip lacks
		std::size_t OwnChanges = 0;     // of Ahead, those with a change of their own: not a merge, not empty
		std::size_t InBase = 0;         // of OwnChanges, those whose patch a commit among Behind also has
		bool Complete = true;           // false when the budget or a missing object stopped the walk while a change of its own still had no twin
		std::vector<PatchTwin> Twins;   // one per own change in the base, newest first, with the newest base commit carrying its patch
		bool MissingObjects = false;    // a patch could not be read for a missing object; the walk stopped there

		friend bool operator==(const PatchMatch&, const PatchMatch&) = default;
	};

	struct PatchIdEntry
	{
		Oid Commit;
		std::optional<Oid> PatchId; // empty: the commit has no patch (a merge, or an empty change)

		friend bool operator==(const PatchIdEntry&, const PatchIdEntry&) = default;
	};

	// Safe to share between workers. A commit's patch id never changes, so nothing is ever evicted.
	class PatchIdCache
	{
	public:
		// Outer empty: not known yet. Inner empty: the commit has no patch (a merge, or an empty change).
		std::optional<std::optional<Oid>> Find(const Oid& commit) const;
		void Store(const Oid& commit, std::optional<Oid> patchId);
		std::size_t GetSize() const;

		// Every commit whose patch id is known, in no particular order.
		std::vector<PatchIdEntry> Entries() const;

	private:
		mutable std::mutex m_Mutex;
		std::unordered_map<Oid, std::optional<Oid>> m_Ids;
	};
}
