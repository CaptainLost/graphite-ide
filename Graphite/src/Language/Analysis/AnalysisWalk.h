#pragma once

#include "Core/Base/Base.h"

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace Graphite {

	// Calls onFile for every file under root whose language has a configured server, skipping
	// everything else. Synchronous, and tested that way — the class below is the thread.
	//
	// Does not consult gitignore: that needs a libgit2 repository handle, which is not safe to
	// share between threads and so is opened by whoever is doing the walking. AnalysisWalk opens
	// one and hands the predicate down.
	void CollectAnalysable(const std::filesystem::path& root, const std::filesystem::path& selectedSolution,
						   const Core::CancellationToken& token, const std::function<void(const std::filesystem::path&)>& onFile);

	// The walk that fills WorkspaceAnalysis's queue, on a worker, cancellable.
	//
	// Separate from WorkspaceAnalysis so that class stays a pure state machine over two ports. A
	// walk is the one part of this feature that genuinely needs a thread and a filesystem, and it
	// answers a question — what is under this root — that has nothing to do with the policy of
	// what to ask a server about.
	class AnalysisWalk
	{
	public:
		~AnalysisWalk();

		AnalysisWalk() = default;
		AnalysisWalk(const AnalysisWalk&) = delete;
		AnalysisWalk& operator=(const AnalysisWalk&) = delete;

		// Cancels a walk still running: what it is finding is about the folder that was open a
		// moment ago. Calling this again is what makes switching folders quickly cost one walk
		// rather than a queue of them.
		void Start(std::filesystem::path root, std::filesystem::path selectedSolution, bool respectGitIgnore);

		// Free when there is nothing to cancel, and safe to call every frame because of it: the
		// render loop does exactly that while the feature is switched off, and this allocates a
		// fresh findings block. The guard lives here rather than at the call site so the next
		// caller inherits it — a per-frame allocation in the render loop is what ADR 0015 calls
		// waste, and a call site is the easy place to forget.
		void Cancel();

		bool IsWalking() const;

		// Moves everything found since the last call into `out`. UI thread only.
		void Take(std::vector<std::filesystem::path>& out);

	private:
		// Held through a shared_ptr, as LanguageServices holds its warm-up findings: a walk still
		// winding down after Cancel needs somewhere valid to write, whether or not anything is
		// still reading.
		struct Findings
		{
			std::mutex Mutex;
			std::vector<std::filesystem::path> Paths;
			bool Walking = false;
		};

		Core::Ref<Findings> m_Findings = Core::CreateRef<Findings>();
		Core::CancellationToken m_Token;

		// Whether a walk has been started and not yet cancelled. Not the same question as
		// IsWalking, which goes false of its own accord the moment the worker runs out of tree:
		// that walk's findings still need dropping, so it is Start and Cancel that move this, not
		// the worker.
		bool m_Live = false;

		// Last, deliberately: destroyed first in reverse declaration order, so the worker is
		// joined before anything it touches goes away.
		Core::JobQueue m_Jobs{1};
	};
}
