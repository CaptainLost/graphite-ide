#pragma once

#include "Core/Git/Lines/CommitGraph.h"
#include "Core/Git/Oid.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The same name always gives the same id, so a failing test prints names, not hashes.
inline Core::Oid OidOf(std::string_view name)
{
	constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
	constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
	constexpr std::size_t kBytesPerWord = 8;
	constexpr std::size_t kBitsPerByte = 8;

	Core::Oid id;

	for (std::size_t word = 0; word < id.Bytes.size(); word += kBytesPerWord)
	{
		std::uint64_t hash = kFnvOffset;

		for (const char c : name)
		{
			hash = (hash ^ static_cast<std::uint8_t>(c)) * kFnvPrime;
		}

		hash = (hash ^ (word + 1)) * kFnvPrime;

		for (std::size_t i = 0; i < kBytesPerWord && word + i < id.Bytes.size(); ++i)
		{
			id.Bytes[word + i] = static_cast<std::uint8_t>(hash >> (kBitsPerByte * i));
		}
	}

	return id;
}

// Names stand for commits. Day 0 is 2026-01-01T00:00Z; Now() is day 100 unless SetNow is called.
class GraphBuilder
{
public:
	static constexpr std::int64_t kDayZero = 1767225600;
	static constexpr std::int64_t kSecondsPerDay = 86400;
	static constexpr int kDefaultNowDay = 100;

	Core::Oid Commit(std::string_view name, std::vector<std::string_view> parents, int day, std::string_view author = "a@example.com")
	{
		Core::GraphCommit commit;
		commit.Id = OidOf(name);

		for (const std::string_view parent : parents)
		{
			commit.Parents.push_back(OidOf(parent));
		}

		commit.Time = Seconds(day);
		commit.AuthorKey = std::string(author);
		m_indexByName[std::string(name)] = m_commits.size();
		m_commits.push_back(std::move(commit));
		return m_commits.back().Id;
	}

	// name1..nameN, each the first parent of the next, the first on `parent` (none for a root), day by day from
	// firstDay, authors taken in turn from `authors`.
	void Chain(std::string_view name, int count, std::optional<std::string_view> parent, int firstDay, std::vector<std::string_view> authors)
	{
		std::string previous = parent.has_value() ? std::string(*parent) : std::string();
		bool hasPrevious = parent.has_value();

		for (int i = 1; i <= count; ++i)
		{
			const std::string current = std::string(name) + std::to_string(i);
			const std::string_view author = authors.empty() ? std::string_view("a@example.com")
															: authors[static_cast<std::size_t>(i - 1) % authors.size()];
			std::vector<std::string_view> parents;

			if (hasPrevious)
			{
				parents.push_back(previous);
			}

			Commit(current, parents, firstDay + i - 1, author);
			previous = current;
			hasPrevious = true;
		}
	}

	// Marks the named commit as a shallow boundary.
	void Cut(std::string_view name)
	{
		m_commits[m_indexByName.at(std::string(name))].ParentsCut = true;
	}

	void SetNow(int day)
	{
		m_nowDay = day;
	}

	Core::CommitGraph Graph() const
	{
		Core::CommitGraph graph;
		graph.Append(m_commits);
		return graph;
	}

	std::int64_t Now() const
	{
		return Seconds(m_nowDay);
	}

	Core::Oid Id(std::string_view name) const
	{
		return OidOf(name);
	}

	static std::int64_t Seconds(int day)
	{
		return kDayZero + static_cast<std::int64_t>(day) * kSecondsPerDay;
	}

private:
	std::vector<Core::GraphCommit> m_commits;
	std::map<std::string, std::size_t> m_indexByName;
	int m_nowDay = kDefaultNowDay;
};
