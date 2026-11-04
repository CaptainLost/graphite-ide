#pragma once

#include "Git/Lines/LineReader.h"

#include "Core/Base/Pointers.h"
#include "Core/Git/Lines/LineModel.h"
#include "Core/Git/Ref.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Graphite::Fixtures {

	// What a hand-built branch is like. The tip commit is committed AgeDays before the builder's Now; no AgeDays means
	// the tip was not read (no TipCommit).
	struct BranchSpec
	{
		bool Head = false;
		std::optional<int> AgeDays = 30;
		Core::BranchState State = Core::BranchState::NotMerged;
		std::string AuthorName = "Ann Author";
		std::string AuthorEmail = "ann@example.com";
		std::size_t Ahead = 0;
		std::size_t Behind = 0;
		bool DistanceKnown = false;
		std::optional<std::string> MissingUpstream;
	};

	// Builds a LineRead by hand. Add refs in the order they should have in LineRead::Refs; the index each Add returns
	// is the ref's index. Build() gives every ref that is not a base a BranchLine and puts the bases in the model in the
	// order MakeBase was called.
	class LineReadBuilder
	{
	public:
		static constexpr std::chrono::sys_seconds Now()
		{
			return std::chrono::sys_seconds{std::chrono::seconds{1'800'000'000}};
		}

		void AddRemote(std::string name) { m_Remotes.push_back(Core::Remote{.Name = std::move(name), .Url = {}}); }

		// "x" -> refs/heads/x.
		std::size_t AddLocal(const std::string& name, const BranchSpec& spec = {})
		{
			return Add(name, "refs/heads/" + name, {}, name, Core::RefKind::Local, spec);
		}

		// ("origin", "x") -> refs/remotes/origin/x, named "origin/x".
		std::size_t AddRemoteBranch(const std::string& remote, const std::string& name, const BranchSpec& spec = {})
		{
			return Add(remote + "/" + name, "refs/remotes/" + remote + "/" + name, remote, name, Core::RefKind::Remote, spec);
		}

		void MakeBase(std::size_t ref, bool active = true) { m_Bases.push_back(Core::LineBase{.Ref = ref, .Active = active}); }

		// The local branch tracks the remote branch and is one row with it (BranchLine::SameRowAs).
		void SameRow(std::size_t local, std::size_t remote)
		{
			m_Entries[local].Ref.UpstreamFullName = m_Entries[remote].Ref.FullName;
			m_SameRowAs.emplace_back(local, remote);
		}

		// The local branch tracks the remote branch without being one row with it (a base that is itself local).
		void Track(std::size_t local, std::size_t remote) { m_Entries[local].Ref.UpstreamFullName = m_Entries[remote].Ref.FullName; }

		LineRead Build() const
		{
			LineRead read;
			read.Remotes = m_Remotes;

			std::vector<Core::Branch> branches;
			branches.reserve(m_Entries.size());

			for (const Entry& entry : m_Entries)
			{
				read.Refs.push_back(entry.Ref);
				branches.push_back(entry.Branch);
			}

			read.Branches = Core::CreateRef<const std::vector<Core::Branch>>(std::move(branches));
			read.Model.Bases = m_Bases;

			for (std::size_t index = 0; index < m_Entries.size(); ++index)
			{
				if (IsBase(index))
				{
					continue;
				}

				Core::BranchLine line;
				line.Ref = index;
				line.State = m_Entries[index].State;

				for (const auto& [local, remote] : m_SameRowAs)
				{
					if (local == index)
					{
						line.SameRowAs = remote;
					}
				}

				read.Model.Branches.push_back(line);
			}

			return read;
		}

	private:
		struct Entry
		{
			Core::LineRef Ref;
			Core::Branch Branch;
			Core::BranchState State = Core::BranchState::NotMerged;
		};

		std::size_t Add(const std::string& branchName, const std::string& fullName, const std::string& remote,
						const std::string& shortName, Core::RefKind kind, const BranchSpec& spec)
		{
			Entry entry;
			entry.Ref = Core::LineRef{.FullName = fullName, .Remote = remote, .ShortName = shortName, .Kind = kind};
			entry.State = spec.State;
			entry.Ref.Tip.Bytes[0] = static_cast<std::uint8_t>(m_Entries.size() + 1); // a tip of its own, so an answer keyed by tip is for one branch
			entry.Branch.Tip = entry.Ref.Tip;

			entry.Branch.Name = branchName;
			entry.Branch.FullName = fullName;
			entry.Branch.Kind = kind;
			entry.Branch.IsHead = spec.Head;
			entry.Branch.Ahead = spec.Ahead;
			entry.Branch.Behind = spec.Behind;
			entry.Branch.DistanceKnown = spec.DistanceKnown;
			entry.Branch.MissingUpstream = spec.MissingUpstream;

			if (spec.AgeDays.has_value())
			{
				Core::CommitSummary tip;
				const std::chrono::sys_seconds when = Now() - std::chrono::hours{24 * *spec.AgeDays};
				tip.Author = Core::Signature{.Name = spec.AuthorName, .Email = spec.AuthorEmail, .When = when};
				tip.Committer = tip.Author;
				entry.Branch.TipCommit = std::move(tip);
			}

			m_Entries.push_back(std::move(entry));
			return m_Entries.size() - 1;
		}

		bool IsBase(std::size_t ref) const
		{
			for (const Core::LineBase& base : m_Bases)
			{
				if (base.Ref == ref)
				{
					return true;
				}
			}

			return false;
		}

		std::vector<Core::Remote> m_Remotes;
		std::vector<Entry> m_Entries;
		std::vector<Core::LineBase> m_Bases;
		std::vector<std::pair<std::size_t, std::size_t>> m_SameRowAs;
	};
}
