#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Result.h"

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

	template<typename Key, typename T, typename Hash = std::hash<Key>>
		requires std::copyable<Key> && std::equality_comparable<Key> &&
				 std::regular_invocable<const Hash&, const Key&> &&
				 std::convertible_to<std::invoke_result_t<const Hash&, const Key&>, std::size_t>
	class LruResultCache
	{
	public:
		// WARNING: UI thread only.
		explicit LruResultCache(std::size_t capacity) : m_Capacity(capacity) {}

		std::optional<std::uint64_t> Request(const Key& key)
		{
			Entry* existingEntry = Find(key);

			if (existingEntry != nullptr)
			{
				if (IsAskedAlready(*existingEntry))
				{
					Touch(*existingEntry);
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

		bool Fail(const Key& key, std::uint64_t generation, std::optional<Core::Error> reason = std::nullopt)
		{
			Entry* entry = Land(key, generation);

			if (entry == nullptr)
			{
				return false;
			}

			entry->Failed = true;
			entry->Reason = std::move(reason);
			return true;
		}

		bool ForgetFailed(const Key& key)
		{
			const Entry* entry = Find(key);

			if (entry == nullptr || !entry->Failed)
			{
				return false;
			}

			m_Entries.erase(key);
			return true;
		}

		std::size_t ForgetFailedWhere(const std::function<bool(const Key&)>& matches)
		{
			return std::erase_if(m_Entries,
								 [&matches](const typename EntryMap::value_type& entry) { return IsFailedMatch(entry, matches); });
		}

		Core::Ref<const T> Get(const Key& key) const
		{
			Entry* entry = Find(key);

			if (entry == nullptr)
			{
				return nullptr;
			}

			Touch(*entry);
			return entry->Value;
		}

		bool IsFailed(const Key& key) const
		{
			const Entry* entry = Find(key);

			return entry != nullptr && entry->Failed;
		}

		// Why the key's answer failed, when the failure was given a reason.
		std::optional<Core::Error> FailureOf(const Key& key) const
		{
			const Entry* entry = Find(key);

			if (entry == nullptr || !entry->Failed)
			{
				return std::nullopt;
			}

			return entry->Reason;
		}

		void Clear() { m_Entries.clear(); }

		std::size_t GetSize() const { return m_Entries.size(); }

	private:
		struct Entry
		{
			// WARNING: Requested comes from the cache-wide m_Generation, never a per-entry counter.
			// Clear erases entries, so a per-entry counter would restart and let an answer from
			// before the clear outrank one from after it.
			std::uint64_t Requested = 0;
			std::uint64_t Applied = 0;

			Core::Ref<const T> Value;
			bool Failed = false;
			std::optional<Core::Error> Reason;

			std::uint64_t LastTouched = 0;
		};

		using EntryMap = std::unordered_map<Key, Entry, Hash>;

		static bool IsInFlight(const Entry& entry) { return entry.Requested != entry.Applied; }

		static bool IsAskedAlready(const Entry& entry)
		{
			return IsInFlight(entry) || entry.Value != nullptr || entry.Failed;
		}

		static bool IsFailedMatch(const typename EntryMap::value_type& entry,
								  const std::function<bool(const Key&)>& matches)
		{
			const auto& [key, held] = entry;
			return held.Failed && matches(key);
		}

		Entry* Find(const Key& key) const
		{
			const typename EntryMap::iterator found = m_Entries.find(key);

			if (found == m_Entries.end())
			{
				return nullptr;
			}

			auto& [foundKey, foundEntry] = *found;
			return &foundEntry;
		}

		void Touch(Entry& entry) const { entry.LastTouched = ++m_Clock; }

		Entry* Land(const Key& key, std::uint64_t generation)
		{
			Entry* entry = Find(key);

			if (entry == nullptr || generation <= entry->Applied)
			{
				return nullptr;
			}

			entry->Applied = generation;
			return entry;
		}

		void EvictIfFull()
		{
			while (m_Entries.size() >= m_Capacity)
			{
				std::optional<Key> oldestKey;
				std::uint64_t oldestTouched = 0;

				for (const auto& [key, entry] : m_Entries)
				{
					if (IsInFlight(entry))
					{
						continue;
					}

					if (!oldestKey.has_value() || entry.LastTouched < oldestTouched)
					{
						oldestKey = key;
						oldestTouched = entry.LastTouched;
					}
				}

				if (!oldestKey.has_value())
				{
					break;
				}

				m_Entries.erase(*oldestKey);
			}
		}

		std::size_t m_Capacity;

		// WARNING: Get is const yet counts as a use, so the entries and the clock are mutable.
		// An entry the reader looks at every frame must not be the one evicted.
		mutable EntryMap m_Entries;
		mutable std::uint64_t m_Clock = 0;
		std::uint64_t m_Generation = 0;
	};
}
