#pragma once

#include "Core/Git/Oid.h"

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace Core {

	struct Tag
	{
		// "release/1.2", where FullName is "refs/tags/release/1.2".
		std::string Name;
		std::string FullName;

		// The commit the tag ends at, however many tag objects lie between.
		Oid Target;

		// An annotated tag's tagger time; a lightweight tag, or an annotated one without a tagger, takes the
		// commit's committer time.
		std::chrono::sys_seconds When;

		bool Annotated = false;

		// An annotated tag's first message line; empty for a lightweight tag.
		std::string Subject;

		friend bool operator==(const Tag&, const Tag&) = default;
	};

	struct TagCacheEntry
	{
		Oid Target;
		std::chrono::sys_seconds When;
		std::string Subject;

		friend bool operator==(const TagCacheEntry&, const TagCacheEntry&) = default;
	};

	// Safe to share between workers. A tag object never changes, so what it peels to is known for good and nothing
	// is ever evicted.
	class TagCache
	{
	public:
		std::optional<TagCacheEntry> Find(const Oid& tagObject) const;
		void Store(const Oid& tagObject, TagCacheEntry entry);
		std::size_t GetSize() const;

	private:
		mutable std::mutex m_Mutex;
		std::unordered_map<Oid, TagCacheEntry> m_Entries;
	};
}
