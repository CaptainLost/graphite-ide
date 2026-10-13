#pragma once

#include "Core/Base/Base.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace Graphite {

	// Answers to questions whose answer never changes - a commit's diff, a file's text at a
	// commit - held once fetched and dropped least recently used first.
	//
	// A key moves through three states: in flight (asked, not yet answered), held (answered
	// with a value) and failed (answered with an error). Request answers "ask the worker"
	// only for a key in none of them, because the views ask about every row they draw on
	// every frame, and a permanent failure must not turn that into a worker job per frame.
	// ForgetFailed is what gives a failed key a way back.
	//
	// UI thread only. Get is const and still counts as a use, so the recency bookkeeping is
	// mutable: an entry the reader is looking at every frame must not be the one evicted.
	template<typename Key, typename T, typename Hash = std::hash<Key>>
		requires std::copyable<Key> && std::equality_comparable<Key> &&
				 std::regular_invocable<const Hash&, const Key&> &&
				 std::convertible_to<std::invoke_result_t<const Hash&, const Key&>, std::size_t>
	class LruResultCache
	{
	public:
		explicit LruResultCache(std::size_t capacity) : m_Capacity(capacity) {}

		// The generation to send the question under, or nothing when the key is already in
		// flight, held or failed - in which case the ask still counts as a use.
		//
		// Evicts only when the key is absent: that is the only case that grows the map, and
		// evicting for a key already present would drop an unrelated entry for no reason.
		std::optional<std::uint64_t> Request(const Key& key)
		{
			const auto existing = m_Entries.find(key);

			if (existing != m_Entries.end())
			{
				const Entry& entry = existing->second;

				if (IsInFlight(entry) || entry.Value != nullptr || entry.Failed)
				{
					Touch(existing->second);
					return std::nullopt;
				}
			}
			else
			{
				EvictIfFull();
			}

			Entry& entry = m_Entries[key];
			Touch(entry);

			entry.Requested = ++m_Generation;
			return entry.Requested;
		}

		// Lands an answer. False when it was dropped: the key was evicted or cleared while the
		// question was out, or a newer answer for it has already landed. Nothing on screen is
		// waiting for a dropped answer.
		bool Succeed(const Key& key, std::uint64_t generation, Core::Ref<const T> value)
		{
			Entry* entry = Land(key, generation);

			if (entry == nullptr)
			{
				return false;
			}

			entry->Value = std::move(value);
			return true;
		}

		// Lands a failure, under the same rules as Succeed. Marked rather than left to imply
		// itself from a null value, so Request can tell "still waiting" from "already tried
		// and lost". There is no older value to keep: an answer that never changes has
		// nothing to fall back to.
		bool Fail(const Key& key, std::uint64_t generation)
		{
			Entry* entry = Land(key, generation);

			if (entry == nullptr)
			{
				return false;
			}

			entry->Failed = true;
			return true;
		}

		// Drops the entry, but only if it failed - a held answer stays, which is what makes
		// this a cache of recently-seen keys rather than of the ones on screen now. True when
		// something was dropped, so the caller can clear the error line that described it.
		bool ForgetFailed(const Key& key)
		{
			const auto entry = m_Entries.find(key);

			if (entry == m_Entries.end() || !entry->second.Failed)
			{
				return false;
			}

			m_Entries.erase(entry);
			return true;
		}

		// ForgetFailed for every failed key `matches` accepts. Answers how many were dropped.
		std::size_t ForgetFailedWhere(const std::function<bool(const Key&)>& matches)
		{
			return std::erase_if(m_Entries,
								 [&matches](const typename EntryMap::value_type& entry) { return IsFailedMatch(entry, matches); });
		}

		// Null while the answer is in flight, after a failure, and for a key never asked about
		// or since evicted.
		Core::Ref<const T> Get(const Key& key) const
		{
			const auto entry = m_Entries.find(key);

			if (entry == m_Entries.end())
			{
				return nullptr;
			}

			Touch(entry->second);
			return entry->second.Value;
		}

		// True until ForgetFailed drops the entry; false while the answer is in flight, once it
		// has arrived whole, and for a key never asked about or since evicted.
		bool IsFailed(const Key& key) const
		{
			const auto entry = m_Entries.find(key);

			return entry != m_Entries.end() && entry->second.Failed;
		}

		// Forgets every entry. The generation counter is left alone, so an answer still in
		// flight from before is dropped when it lands.
		void Clear() { m_Entries.clear(); }

		std::size_t GetSize() const { return m_Entries.size(); }

	private:
		struct Entry
		{
			// From the cache-wide m_Generation, never from a counter local to the entry: Clear
			// erases the entry itself, and an entry recreated afterwards starts its Applied back
			// at zero. If Requested also restarted at a small number local to the entry, an
			// answer from before the clear could outrank one from after it. The shared counter
			// makes every generation unique for the cache's lifetime, so whichever answer
			// carries the larger one is the newer, clear or no clear.
			std::uint64_t Requested = 0;
			std::uint64_t Applied = 0;

			Core::Ref<const T> Value;
			bool Failed = false;

			// Counts up on every request and every read, so eviction can pick the entry nobody
			// has looked at for longest. A counter rather than a clock: the cache must stay
			// testable without one.
			std::uint64_t LastTouched = 0;
		};

		using EntryMap = std::unordered_map<Key, Entry, Hash>;

		static bool IsInFlight(const Entry& entry) { return entry.Requested != entry.Applied; }

		static bool IsFailedMatch(const typename EntryMap::value_type& entry,
								  const std::function<bool(const Key&)>& matches)
		{
			const auto& [key, held] = entry;
			return held.Failed && matches(key);
		}

		void Touch(Entry& entry) const { entry.LastTouched = ++m_Clock; }

		// The entry an answer under this generation should land in, marked as answered; null
		// when the answer is to be dropped.
		Entry* Land(const Key& key, std::uint64_t generation)
		{
			const auto entry = m_Entries.find(key);

			if (entry == m_Entries.end() || generation <= entry->second.Applied)
			{
				return nullptr;
			}

			entry->second.Applied = generation;
			return &entry->second;
		}

		void EvictIfFull()
		{
			// A loop, not one shot: a single eviction only ever drops one entry, so once the map
			// is above the bound - which a run of requests all still in flight can leave it at,
			// since none of them were evictable at the time - one-in-one-out on every later
			// request would hold it there forever instead of bringing it back down.
			while (m_Entries.size() >= m_Capacity)
			{
				auto oldest = m_Entries.end();

				for (auto entry = m_Entries.begin(); entry != m_Entries.end(); ++entry)
				{
					// An entry still in flight is never evicted: its answer would then arrive for
					// a key nobody holds and be dropped, and the reader would sit looking at a row
					// that never fills in.
					if (IsInFlight(entry->second))
					{
						continue;
					}

					if (oldest == m_Entries.end() || entry->second.LastTouched < oldest->second.LastTouched)
					{
						oldest = entry;
					}
				}

				// Nothing evictable this pass - every remaining entry is in flight. Stop rather
				// than spin: the map stays over the bound until one of them lands.
				if (oldest == m_Entries.end())
				{
					break;
				}

				m_Entries.erase(oldest);
			}
		}

		std::size_t m_Capacity;

		mutable std::unordered_map<Key, Entry, Hash> m_Entries;
		mutable std::uint64_t m_Clock = 0;
		std::uint64_t m_Generation = 0;
	};
}
