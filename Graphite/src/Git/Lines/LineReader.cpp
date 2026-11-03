#include "Git/Lines/LineReader.h"

#include "Git/Lines/GraphFile.h"

#include "Core/Base/Log.h"
#include "Core/Git/Lines/AncestryGraph.h"
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
	}

	std::vector<Core::LineRef> LineRefsOf(const std::vector<Core::Branch>& branches, const std::vector<std::string>& remotes)
	{
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
					ref.UpstreamFullName = std::string{Core::kRemoteBranchPrefix} + *branch.Upstream;
				}
			}

			refs.push_back(std::move(ref));
		}

		return refs;
	}

	Core::Result<LineRead> ReadLines(const Core::Repository& repository, const LineStorePaths& store, std::int64_t now)
	{
		const Core::Result<std::vector<Core::Branch>> branches = repository.QueryBranches();
		if (!branches.HasValue())
		{
			return branches.GetError();
		}

		const Core::Result<std::vector<std::string>> remotes = repository.QueryRemoteNames();
		if (!remotes.HasValue())
		{
			return remotes.GetError();
		}

		LineRead read;
		read.Refs = LineRefsOf(branches.GetValue(), remotes.GetValue());

		Core::AncestryGraph graph = LoadGraph(store.Graph).value_or(Core::AncestryGraph{});
		bool graphChanged = false;

		// Once the repository is deepened the cut commits would stay cut for ever, so the whole graph is read again.
		if (!repository.IsShallow() && HasCutCommit(graph))
		{
			graph = Core::AncestryGraph{};
			graphChanged = true;
		}

		Core::GraphReadRequest request;
		std::unordered_set<Core::Oid> seen;

		for (const Core::LineRef& ref : read.Refs)
		{
			if (!ref.Tip.IsZero() && seen.insert(ref.Tip).second && graph.Find(ref.Tip) == Core::AncestryGraph::kNoCommit)
			{
				request.Tips.push_back(ref.Tip);
			}
		}

		if (!request.Tips.empty())
		{
			request.Known = HeadsOf(graph);

			Core::Result<std::vector<Core::GraphCommit>> commits = repository.ReadGraphCommits(request);
			if (!commits.HasValue())
			{
				return commits.GetError();
			}

			read.CommitsRead = commits.GetValue().size();
			graph.Append(std::move(commits).GetValue());
			graphChanged = graphChanged || read.CommitsRead > 0;
		}

		const bool answersAreForeign = HasForeignVersion(store.Answers);
		read.Answers = LoadLineAnswers(store.Answers);
		const Core::LineAnswers loaded = read.Answers;

		read.Model = Core::ComputeLines(Core::LineModelInput{graph, read.Refs, read.Answers, now});

		for (const Core::LineBase& base : read.Model.Bases)
		{
			const bool found = base.Reason == Core::BaseReason::MergedLines || base.Reason == Core::BaseReason::Authors;
			const std::string& name = read.Refs[base.Ref].FullName;

			if (found && !Contains(read.Answers.KeptBases, name))
			{
				read.Answers.KeptBases.push_back(name);
			}
		}

		if (graphChanged)
		{
			const Core::Result<void> saved = SaveGraph(store.Graph, graph);
			if (!saved.HasValue())
			{
				APP_WARN("Could not save the commit graph cache: {}", saved.GetError().Message);
			}
		}

		if (!(read.Answers == loaded) && !answersAreForeign)
		{
			const Core::Result<void> saved = SaveLineAnswers(store.Answers, read.Answers);
			if (!saved.HasValue())
			{
				APP_WARN("Could not save the line answers: {}", saved.GetError().Message);
			}
		}

		return read;
	}
}
