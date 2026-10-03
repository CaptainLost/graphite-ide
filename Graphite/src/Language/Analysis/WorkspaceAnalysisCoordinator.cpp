#include "Language/Analysis/WorkspaceAnalysisCoordinator.h"

#include <utility>
#include <vector>

namespace Graphite {

	WorkspaceAnalysisCoordinator::WorkspaceAnalysisCoordinator(WorkspaceServersPort& servers,
															   WorkspaceAnalysisPort& port,
															   FileTextSource& texts,
															   AnalysisCache& cache)
		: m_Servers(servers), m_Cache(cache), m_Pass(port, texts, cache, &m_Store)
	{
	}

	void WorkspaceAnalysisCoordinator::Update(const AnalysisSwitches& switches)
	{
		// Trust folds into the switch rather than beside it: every edge below - walk, queue,
		// store - already follows this flag, so an untrusted folder is simply one where the
		// analysis is off, and trusting it is the same rising edge as turning the setting on.
		m_Enabled = switches.SettingEnabled && switches.Trusted;

		// The rising edge of that flag: switching the feature off cancels the walk and empties the
		// queue, so switching it back on has nothing left to run - without this, it would stay
		// inert until the folder was reopened, which reads as a switch that does not work.
		const bool switchedOn = m_Enabled && !m_WasEnabled;

		// The falling edge, for the store alone. The walk is cancelled unconditionally in Tick,
		// which it can be because Cancel on a walk that is not running does nothing; SetRoot
		// cannot be called that way, because every call tears down the current listing and submits
		// a new one, and a frame that did that sixty times a second would never be ready.
		const bool switchedOff = !m_Enabled && m_WasEnabled;

		m_WasEnabled = m_Enabled;

		const bool rootChanged = switches.Root != m_WarmedRoot;

		// Any change to the list, from anywhere, reaches the servers here - before Poll, so a
		// server trust has just started is drained on this same frame.
		if (switches.TrustRevision != m_SeenTrustRevision)
		{
			m_SeenTrustRevision = switches.TrustRevision;
			m_Servers.Revalidate();
		}

		const bool trustSwitchedOn = switches.Trusted && !m_WasTrusted;
		m_WasTrusted = switches.Trusted;

		// The language servers' fallback root is not set here: it must be in place before a
		// folder's tabs are restored, which happens before this per-frame check runs, so it is set
		// from the workspace events instead.
		if (rootChanged)
		{
			m_WarmedRoot = switches.Root;

			if (!m_WarmedRoot.empty() && switches.Trusted)
			{
				// Ignored trees skipped: this walk exists to find one source file per language as
				// cheaply as it can, and build outputs hold plenty of files that answer nothing.
				m_Servers.WarmUp(m_WarmedRoot, true);
			}

			// Outside the branch, unlike the warm-up: a folder closing has to drop the queue as
			// surely as a folder opening replaces it.
			m_Pass.Clear();

			// The store follows the folder for the same reason the walk does: an analysis kept for
			// one workspace says nothing about the next. An empty root detaches it, which is what
			// "no folder is open" looks like from there.
			//
			// And the empty root is what a switched-off feature looks like from there as well.
			// Handing the store the real root while the analysis is off would create, sweep and
			// fill `.graphite/analysis` under the tree of a user who asked for none of it, and the
			// store's detached path already refuses every lookup and drops every write.
			m_Store.SetRoot(m_Enabled ? m_WarmedRoot : std::filesystem::path{});

			if (!m_WarmedRoot.empty() && m_Enabled)
			{
				m_Walk.Start(m_WarmedRoot, true);
			}
			else
			{
				m_Walk.Cancel();
			}
		}
		else if (switchedOn && !m_WarmedRoot.empty())
		{
			// An `else if` rather than a branch of its own: a frame that changed the root has
			// already started the walk for it above, and this must not start a second one over the
			// top. Nothing to start with no folder open, hence the root check. The queue needs no
			// clearing: switching the feature off emptied it on the way down.
			m_Walk.Start(m_WarmedRoot, true);

			// Re-attached with the walk, because this is the transition that undoes the detach: a
			// pass restarted against a detached store would ask the server about every file the
			// last session already answered for.
			m_Store.SetRoot(m_WarmedRoot);
		}
		else if (switchedOff)
		{
			// The other half of the same rule, and no root check: detaching a store that was
			// already detached costs one empty listing and keeps the two edges symmetrical.
			m_Store.SetRoot({});
		}

		// The folder stayed and became trusted. A folder that arrived trusted was warmed in the
		// root-change branch above, which is why this is not part of it.
		if (trustSwitchedOn && !rootChanged)
		{
			m_Servers.WarmUp(m_WarmedRoot, true);
		}
	}

	void WorkspaceAnalysisCoordinator::Tick(const AnalysisTickInputs& inputs)
	{
		// Read every frame rather than on a change notification: both are a comparison against a
		// value that almost never differs. Both setters return without work when nothing moved.
		m_Cache.SetBudget(inputs.BudgetBytes);
		m_Pass.SetEnabled(m_Enabled);
		m_Pass.SetConcurrency(inputs.Concurrency);

		if (!m_Enabled)
		{
			// Cancelled rather than merely ignored. A walk left running would keep a worker
			// traversing the whole tree to fill a queue Enqueue now drops on the floor.
			m_Walk.Cancel();
		}

		// Read before the drain below, not after. The walker can push its last findings and clear
		// this flag between the two calls, and a frame that reported "not walking" while still
		// holding undrained paths would show a Total that jumps the frame after - the count
		// appearing to go backwards. Reading it first costs at worst one extra frame of "finding
		// files", which is the harmless direction.
		const bool walking = m_Enabled && m_Walk.IsWalking();

		// Drained every frame rather than once at the end: a tree of any size takes a while to
		// walk, and there is no reason for the first file found to wait for the last.
		std::vector<std::filesystem::path> found;
		m_Walk.Take(found);

		m_Pass.SetWalking(walking);
		m_Pass.SetTabWidth(inputs.TabWidth);
		m_Pass.Enqueue(std::move(found));

		// Before the pass ticks: this is what turns a finished index listing into an answerable
		// store, and a Tick ahead of it spends a frame asking a store that cannot yet say what it
		// already knows. Unconditional, and so is the Tick: the store states its own readiness to
		// the pass, and gating the Tick here would also stop the pass finishing files whose
		// answers have arrived and timing out ones that have gone silent.
		m_Store.Poll();

		m_Pass.Tick(inputs.Now, inputs.ForegroundBusy, inputs.HeldElsewhere);
	}
}
