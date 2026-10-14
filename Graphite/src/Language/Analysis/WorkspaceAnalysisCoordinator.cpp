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
		const bool respectGitIgnore = true;
		const std::filesystem::path detachedRoot;

		m_Enabled = switches.SettingEnabled && switches.Trusted;

		const bool switchedOn = m_Enabled && !m_WasEnabled;

		// WARNING: The walk is cancelled unconditionally in Tick because cancelling an idle walk
		// does nothing; SetRoot cannot be called that way, as every call tears down the listing
		// and submits a new one. The store therefore reacts to the falling edge only.
		const bool switchedOff = !m_Enabled && m_WasEnabled;

		m_WasEnabled = m_Enabled;

		const bool rootChanged = switches.Root != m_WarmedRoot;
		const bool solutionChanged = switches.SelectedSolution != m_WalkedSolution;

		if (solutionChanged)
		{
			m_WalkedSolution = switches.SelectedSolution;
		}

		if (switches.TrustRevision != m_SeenTrustRevision)
		{
			m_SeenTrustRevision = switches.TrustRevision;
			m_Servers.Revalidate();
		}

		const bool trustSwitchedOn = switches.Trusted && !m_WasTrusted;
		m_WasTrusted = switches.Trusted;

		// WARNING: The language servers' fallback root is not set here; it must be in place
		// before a folder's tabs are restored, so it is set from the workspace events.
		if (rootChanged)
		{
			m_WarmedRoot = switches.Root;

			if (!m_WarmedRoot.empty() && switches.Trusted)
			{
				m_Servers.WarmUp(m_WarmedRoot, respectGitIgnore);
			}

			m_Pass.Clear();

			// WARNING: A switched-off analysis hands the store an empty root. The real root
			// would create and fill `.graphite/analysis` under a tree whose user asked for none.
			m_Store.SetRoot(m_Enabled ? m_WarmedRoot : detachedRoot);

			if (!m_WarmedRoot.empty() && m_Enabled)
			{
				m_Walk.Start(m_WarmedRoot, m_WalkedSolution, respectGitIgnore);
			}
			else
			{
				m_Walk.Cancel();
			}
		}
		else if (switchedOn && !m_WarmedRoot.empty())
		{
			m_Walk.Start(m_WarmedRoot, m_WalkedSolution, respectGitIgnore);
			m_Store.SetRoot(m_WarmedRoot);
		}
		else if (solutionChanged && m_Enabled && !m_WarmedRoot.empty())
		{
			m_Pass.Clear();
			m_Walk.Start(m_WarmedRoot, m_WalkedSolution, respectGitIgnore);
		}
		else if (switchedOff)
		{
			m_Store.SetRoot(detachedRoot);
		}

		if (trustSwitchedOn && !rootChanged)
		{
			m_Servers.WarmUp(m_WarmedRoot, respectGitIgnore);
		}
	}

	void WorkspaceAnalysisCoordinator::Tick(const AnalysisTickInputs& inputs)
	{
		m_Cache.SetBudget(inputs.BudgetBytes);
		m_Pass.SetEnabled(m_Enabled);
		m_Pass.SetConcurrency(inputs.Concurrency);

		if (!m_Enabled)
		{
			m_Walk.Cancel();
		}

		// WARNING: Read before the drain below. The walker can push its last findings and clear
		// this flag between the two calls, and reporting "not walking" while holding undrained
		// paths would make the Total count appear to go backwards.
		const bool walking = m_Enabled && m_Walk.IsWalking();

		std::vector<std::filesystem::path> foundPaths;
		m_Walk.Take(foundPaths);

		m_Pass.SetWalking(walking);
		m_Pass.SetTabWidth(inputs.TabWidth);
		m_Pass.Enqueue(std::move(foundPaths));

		// WARNING: The store must be polled before the pass ticks, and the pass must tick
		// unconditionally; it finishes answered files and times out silent ones.
		m_Store.Poll();

		m_Pass.Tick(inputs.Now, inputs.ForegroundBusy, inputs.HeldElsewhere);
	}
}
