#pragma once

#include "Core/Git/Oid.h"

#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace Core {

	struct PatchMatch
	{
		std::size_t Ahead = 0;      // commits tip has that base lacks, merges included
		std::size_t Behind = 0;     // commits base has that tip lacks
		std::size_t OwnChanges = 0; // of Ahead, those with a change of their own: not a merge, not empty
		std::size_t InBase = 0;     // of OwnChanges, those whose patch a commit among Behind also has
		bool Complete = true;       // false when Behind holds more commits than the budget let it read

		friend bool operator==(const PatchMatch&, const PatchMatch&) = default;
	};

	// Safe to share between workers. A commit's patch id never changes, so nothing is ever evicted.
	class PatchIdCache
	{
	public:
		// Outer empty: not known yet. Inner empty: the commit has no patch (a merge, or an empty change).
		std::optional<std::optional<Oid>> Find(const Oid& commit) const;
		void Store(const Oid& commit, std::optional<Oid> patchId);
		std::size_t GetSize() const;

	private:
		mutable std::mutex m_Mutex;
		std::unordered_map<Oid, std::optional<Oid>> m_Ids;
	};
}
