#include "Git/Lines/LineReader.h"

#include "Git/Lines/GraphFile.h"

#include "Core/Base/Log.h"
#include "Core/Git/Lines/LineQuestions.h"
#include "Core/Git/Requests.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace Graphite {

	namespace {
		// The longest remote name followed by a slash that prefixes the branch name; empty when none does.
		std::string_view RemoteOf(std::string_view branchName, const std::vector<std::string>& remotes)
		{
			std::string_view best;

			for (const std::string& remote : remotes)
			{
				const bool prefixes = branchName.size() > remote.size() && branchName.starts_with(remote) && branchName[remote.size()] == '/';

				if (prefixes && remote.size() > best.size())
				{
					best = remote;
				}
			}

			return best;
		}

		bool HasCutCommit(const Core::AncestryGraph& graph)
		{
			for (Core::AncestryGraph::Index commit = 0; commit < graph.Size(); ++commit)
			{
				if (graph.ParentsCutAt(commit))
				{
					return true;
				}
			}

			return false;
		}

		// The cache's heads: commits no other cached commit has as a parent. Hiding them hides the whole cache.
		std::vector<Core::Oid> HeadsOf(const Core::AncestryGraph& graph)
		{
			std::vector<bool> hasChild(graph.Size(), false);

			for (Core::AncestryGraph::Index commit = 0; commit < graph.Size(); ++commit)
			{
				for (const Core::AncestryGraph::Index parent : graph.ParentsOf(commit))
				{
					hasChild[parent] = true;
				}
			}

			std::vector<Core::Oid> heads;
			for (Core::AncestryGraph::Index commit = 0; commit < graph.Size(); ++commit)
			{
				if (!hasChild[commit])
				{
					heads.push_back(graph.IdOf(commit));
				}
			}

			return heads;
		}

		bool Contains(const std::vector<std::string>& names, const std::string& name)
		{
			return std::ranges::find(names, name) != names.end();
		}

		// Only an added or a kept base has a place in the base order.
		bool IsAddedOrKept(const Core::LineAnswers& answers, const std::string& name)
		{
			return Contains(answers.AddedBases, name) || Contains(answers.KeptBases, name);
		}

		std::vector<std::string> NamesOf(const std::vector<Core::Remote>& remotes)
		{
			std::vector<std::string> names;
			names.reserve(remotes.size());

			for (const Core::Remote& remote : remotes)
			{
				names.push_back(remote.Name);
			}

			return names;
		}

		// The graph a read works on. One it is given is shared with the read before and is never changed: appending to
		// it appends to a copy, and a read that appends nothing hands the same pointer back.
		class WorkingGraph
		{
		public:
			explicit WorkingGraph(Core::Ref<const Core::AncestryGraph> shared) : m_Shared(std::move(shared)) {}
			explicit WorkingGraph(Core::AncestryGraph owned) : m_Owned(std::move(owned)) {}

			const Core::AncestryGraph& Get() const { return m_Owned.has_value() ? *m_Owned : *m_Shared; }

			void StartAfresh()
			{
				m_Owned.emplace();
				m_Shared.reset();
			}

			void Append(std::vector<Core::GraphCommit> commits)
			{
				if (!m_Owned.has_value())
				{
					m_Owned = *m_Shared;
					m_Shared.reset();
				}

				m_Owned->Append(std::move(commits));
			}

			Core::Ref<const Core::AncestryGraph> Share() &&
			{
				return m_Owned.has_value() ? Core::CreateRef<const Core::AncestryGraph>(std::move(*m_Owned)) : m_Shared;
			}

		private:
			Core::Ref<const Core::AncestryGraph> m_Shared;
			std::optional<Core::AncestryGraph> m_Owned;
		};

		// Each tip of the graph walk once: the branches' and the tags' commits. A branch whose tip git cannot read as a
		// commit (a missing object, or one that is no commit) is left out: asking git to walk from it would fail the whole
		// read, and the model reports it as TipMissing. A tag is listed only when it ends at a commit git has, so every
		// tag's commit is readable. `tags` may be null.
		std::vector<Core::Oid> ReadableTips(const std::vector<Core::Branch>& branches, const std::vector<Core::Tag>* tags)
		{
			std::vector<Core::Oid> tips;
			std::unordered_set<Core::Oid> seen;

			for (const Core::Branch& branch : branches)
			{
				if (branch.TipCommit.has_value() && seen.insert(branch.Tip).second)
				{
					tips.push_back(branch.Tip);
				}
			}

			if (tags == nullptr)
			{
				return tips;
			}

			for (const Core::Tag& tag : *tags)
			{
				if (seen.insert(tag.Target).second)
				{
					tips.push_back(tag.Target);
				}
			}

			return tips;
		}

		// Answers about refs that are gone: a kept base or a comparison naming a ref the read did not list, and the place
		// in the base order of a base no longer added or kept. A removed base stays removed, so that a branch pushed
		// again under the same name does not come back as a base.
		void DropAnswersAboutGoneRefs(Core::LineAnswers& answers, const std::vector<Core::LineRef>& refs)
		{
			std::unordered_set<std::string_view> names;

			for (const Core::LineRef& ref : refs)
			{
				names.insert(ref.FullName);
			}

			const auto isGone = [&names](const std::string& name) { return !names.contains(std::string_view{name}); };
			std::erase_if(answers.KeptBases, isGone);
			std::erase_if(answers.BaseOrder, [&answers](const std::string& name) { return !IsAddedOrKept(answers, name); });
			std::erase_if(answers.Comparisons,
						  [&isGone](const auto& entry) { return isGone(entry.first) || (entry.second.has_value() && isGone(*entry.second)); });
		}

		// A base the rules found is kept and takes the next place in the base order the first time; one the user
		// stopped never is.
		void AddKeptBasesFound(Core::LineAnswers& answers, const Core::LineModel& model, const std::vector<Core::LineRef>& refs)
		{
			for (const Core::LineBase& base : model.Bases)
			{
				const bool found = base.Reason == Core::BaseReason::MergedLines || base.Reason == Core::BaseReason::Authors;
				const std::string& name = refs[base.Ref].FullName;

				if (!found || Contains(answers.RemovedBases, name))
				{
					continue;
				}

				if (!Contains(answers.KeptBases, name))
				{
					answers.KeptBases.push_back(name);
				}

				if (!Contains(answers.BaseOrder, name))
				{
					answers.BaseOrder.push_back(name);
				}
			}
		}
	}

	std::vector<Core::LineRef> LineRefsOf(const std::vector<Core::Branch>& branches, const std::vector<std::string>& remotes)
	{
		std::unordered_set<std::string_view> remoteBranchNames;
		std::unordered_set<std::string_view> localBranchNames;

		for (const Core::Branch& branch : branches)
		{
			if (branch.Kind == Core::RefKind::Remote)
			{
				remoteBranchNames.insert(branch.Name);
			}
			else
			{
				localBranchNames.insert(branch.Name);
			}
		}

		std::vector<Core::LineRef> refs;
		refs.reserve(branches.size());

		for (const Core::Branch& branch : branches)
		{
			Core::LineRef ref;
			ref.FullName = branch.FullName;
			ref.Tip = branch.Tip;
			ref.Kind = branch.Kind;
			ref.IsRemoteDefault = branch.IsRemoteDefault;

			if (branch.Kind == Core::RefKind::Remote)
			{
				const std::string_view remote = RemoteOf(branch.Name, remotes);
				ref.Remote = std::string{remote};
				ref.ShortName = remote.empty() ? branch.Name : branch.Name.substr(remote.size() + 1);
			}
			else
			{
				ref.ShortName = branch.Name;

				if (branch.Upstream.has_value())
				{
					const bool tracksALocalBranch = !remoteBranchNames.contains(*branch.Upstream) && localBranchNames.contains(*branch.Upstream);
					ref.UpstreamFullName = tracksALocalBranch ? Core::LocalBranchReference(*branch.Upstream)
															  : std::string{Core::kRemoteBranchPrefix} + *branch.Upstream;
				}
			}

			refs.push_back(std::move(ref));
		}

		return refs;
	}

	Core::LineAnswers SaveAnswerChanges(const std::filesystem::path& file, const Core::LineAnswers& loaded, const Core::LineAnswers& computed)
	{
		const std::optional<Core::LineAnswers> current = LoadLineAnswersToUpdate(file);

		if (!current.has_value())
		{
			return computed;
		}

		Core::LineAnswers merged = *current;

		// A base the user stopped while the read ran is not kept again.
		for (const std::string& name : computed.KeptBases)
		{
			if (!Contains(loaded.KeptBases, name) && !Contains(merged.KeptBases, name) && !Contains(merged.RemovedBases, name))
			{
				merged.KeptBases.push_back(name);
			}
		}

		for (const std::string& name : loaded.KeptBases)
		{
			if (!Contains(computed.KeptBases, name))
			{
				std::erase(merged.KeptBases, name);
			}
		}

		// Places the user gave meanwhile stay where they are; the read's new ones follow them.
		for (const std::string& name : computed.BaseOrder)
		{
			if (!Contains(loaded.BaseOrder, name) && !Contains(merged.BaseOrder, name) && IsAddedOrKept(merged, name))
			{
				merged.BaseOrder.push_back(name);
			}
		}

		for (const std::string& name : loaded.BaseOrder)
		{
			if (!Contains(computed.BaseOrder, name) && !IsAddedOrKept(merged, name))
			{
				std::erase(merged.BaseOrder, name);
			}
		}

		// A comparison the read dropped goes only if nobody changed it since.
		for (const auto& [branch, target] : loaded.Comparisons)
		{
			const auto stillThere = merged.Comparisons.find(branch);

			if (!computed.Comparisons.contains(branch) && stillThere != merged.Comparisons.end() && stillThere->second == target)
			{
				merged.Comparisons.erase(stillThere);
			}
		}

		if (merged == *current)
		{
			return merged;
		}

		const Core::Result<void> saved = SaveLineAnswers(file, merged);
		if (!saved.HasValue())
		{
			APP_WARN("Could not save the line answers: {}", saved.GetError().Message);
		}

		return merged;
	}

	Core::Result<LineRead> ReadLines(const Core::Repository& repository, const LineStorePaths& store, std::int64_t now)
	{
		Core::Result<std::vector<Core::Branch>> branches = repository.QueryBranches();
		if (!branches.HasValue())
		{
			return branches.GetError();
		}

		return ReadLines(repository, store, LineReadInput{.Branches = Core::CreateRef<const std::vector<Core::Branch>>(std::move(branches).GetValue()), .Now = now});
	}

	Core::Result<LineRead> ReadLines(const Core::Repository& repository, const LineStorePaths& store, const LineReadInput& input)
	{
		CORE_ASSERT(input.Branches != nullptr, "a read describes a branch snapshot");

		Core::Result<std::vector<Core::Remote>> remotes = repository.QueryRemotes();
		if (!remotes.HasValue())
		{
			return remotes.GetError();
		}

		LineRead read;
		read.Branches = input.Branches;
		read.Tags = input.Tags;
		read.Remotes = std::move(remotes).GetValue();
		read.Refs = LineRefsOf(*read.Branches, NamesOf(read.Remotes));

		WorkingGraph graph = input.Graph != nullptr ? WorkingGraph{input.Graph} : WorkingGraph{LoadGraph(store.Graph).value_or(Core::AncestryGraph{})};
		bool graphChanged = false;

		// Once the repository is deepened the cut commits would stay cut for ever, so the whole graph is read again.
		if (!repository.IsShallow() && HasCutCommit(graph.Get()))
		{
			graph.StartAfresh();
			graphChanged = true;
		}

		// WARNING: The tags' commits are tips of the walk and never refs: the model is computed on read.Refs, the branches.
		const std::vector<Core::Oid> readableTips = ReadableTips(*read.Branches, read.Tags.get());
		Core::GraphReadRequest request;

		for (const Core::Oid& tip : readableTips)
		{
			if (graph.Get().Find(tip) == Core::AncestryGraph::kNoCommit)
			{
				request.Tips.push_back(tip);
			}
		}

		if (!request.Tips.empty())
		{
			request.Known = HeadsOf(graph.Get());

			Core::Result<std::vector<Core::GraphCommit>> commits = repository.ReadGraphCommits(request);
			if (!commits.HasValue() && !request.Known.empty())
			{
				// Only a fallback, since a cached head git lacks is passed over: whatever else went wrong, the read
				// starts once more from nothing.
				graph.StartAfresh();
				graphChanged = true;
				request.Known.clear();
				request.Tips = readableTips;
				commits = repository.ReadGraphCommits(request);
			}

			if (!commits.HasValue())
			{
				return commits.GetError();
			}

			read.CommitsRead = commits.GetValue().size();

			if (read.CommitsRead > 0)
			{
				graph.Append(std::move(commits).GetValue());
				graphChanged = true;
			}
		}

		const Core::LineAnswers loaded = LoadLineAnswers(store.Answers);
		read.Model = Core::ComputeLines(Core::LineModelInput{graph.Get(), read.Refs, loaded, input.Now});
		read.Now = input.Now;
		read.Store = store;

		Core::LineAnswers computed = loaded;
		AddKeptBasesFound(computed, read.Model, read.Refs);
		DropAnswersAboutGoneRefs(computed, read.Refs);

		if (graphChanged)
		{
			const Core::Result<void> saved = SaveGraph(store.Graph, graph.Get());
			if (!saved.HasValue())
			{
				APP_WARN("Could not save the commit graph cache: {}", saved.GetError().Message);
			}
		}

		read.Answers = computed == loaded ? loaded : SaveAnswerChanges(store.Answers, loaded, computed);
		read.Graph = std::move(graph).Share();

		const bool generationsHeld = read.Graph == input.Graph && input.Generations != nullptr;
		read.Generations = generationsHeld ? input.Generations : Core::CreateRef<const std::vector<std::uint32_t>>(Core::GenerationsOf(*read.Graph));
		return read;
	}
}
