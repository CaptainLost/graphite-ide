#pragma once

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineModel.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
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
	static constexpr std::size_t kNoRef = std::numeric_limits<std::size_t>::max();

	// The hosting service, which commits the pull requests it merges: a committer for work that lands on a shared line.
	static constexpr std::string_view kService = "service@example.com";

	// With no committer, the author committed the commit.
	Core::Oid Commit(std::string_view name, std::vector<std::string_view> parents, int day, std::string_view author = "a@example.com",
					 std::optional<std::string_view> committer = std::nullopt)
	{
		Core::GraphCommit commit;
		commit.Id = OidOf(name);

		for (const std::string_view parent : parents)
		{
			commit.Parents.push_back(OidOf(parent));
		}

		commit.Time = Seconds(day);
		commit.AuthorKey = std::string(author);
		commit.CommittedByAuthor = !committer.has_value() || *committer == author;
		m_indexByName[std::string(name)] = m_commits.size();
		m_commits.push_back(std::move(commit));
		return m_commits.back().Id;
	}

	// name1..nameN, each the first parent of the next, the first on `parent` (none for a root), day by day from
	// firstDay, authors taken in turn from `authors`, every one committed by `committer` (none: by its author).
	void Chain(std::string_view name, int count, std::optional<std::string_view> parent, int firstDay, std::vector<std::string_view> authors,
			   std::optional<std::string_view> committer = std::nullopt)
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

			Commit(current, parents, firstDay + i - 1, author, committer);
			previous = current;
			hasPrevious = true;
		}
	}

	// Marks the named commit as a shallow boundary.
	void Cut(std::string_view name)
	{
		m_commits[m_indexByName.at(std::string(name))].ParentsCut = true;
	}

	// "refs/heads/x" is local; "refs/remotes/<remote>/<short>" is remote (remote names in tests have no slash).
	// `at` names the tip commit; a name never committed gives a tip the graph lacks.
	std::size_t Ref(std::string_view fullName, std::string_view at, bool remoteDefault = false,
					std::optional<std::string_view> upstreamFullName = std::nullopt)
	{
		Core::LineRef ref;
		ref.FullName = std::string(fullName);
		ref.Tip = OidOf(at);
		ref.IsRemoteDefault = remoteDefault;

		if (upstreamFullName.has_value())
		{
			ref.UpstreamFullName = std::string(*upstreamFullName);
		}

		if (fullName.starts_with(Core::kRemoteBranchPrefix))
		{
			const std::string_view remoteAndShortName = fullName.substr(Core::kRemoteBranchPrefix.size());
			const std::size_t remoteEnd = remoteAndShortName.find('/');
			ref.Kind = Core::RefKind::Remote;
			ref.Remote = std::string(remoteAndShortName.substr(0, remoteEnd));
			ref.ShortName = std::string(remoteAndShortName.substr(remoteEnd + 1));
		}
		else
		{
			ref.Kind = Core::RefKind::Local;
			ref.ShortName = Core::ShortBranchName(fullName);
		}

		m_refs.push_back(std::move(ref));
		return m_refs.size() - 1;
	}

	std::span<const Core::LineRef> Refs() const
	{
		return m_refs;
	}

	// kNoRef for a name Ref was never given, so a check against it fails rather than matching ref 0.
	std::size_t RefIndex(std::string_view fullName) const
	{
		for (std::size_t refIndex = 0; refIndex < m_refs.size(); ++refIndex)
		{
			if (m_refs[refIndex].FullName == fullName)
			{
				return refIndex;
			}
		}

		return kNoRef;
	}

	void SetNow(int day)
	{
		m_nowDay = day;
	}

	Core::AncestryGraph Graph() const
	{
		Core::AncestryGraph graph;
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
	std::vector<Core::LineRef> m_refs;
	int m_nowDay = kDefaultNowDay;
};
