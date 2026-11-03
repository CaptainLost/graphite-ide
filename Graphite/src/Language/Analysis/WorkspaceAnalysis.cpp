#include "Language/Analysis/WorkspaceAnalysis.h"

#include "Language/Analysis/AnalysisStore.h"
#include "Language/Registry/LanguageRegistry.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <utility>

namespace Graphite {

	namespace {

		constexpr double kAnalysisTimeoutSeconds = 120.0;
		constexpr double kForegroundYieldCapSeconds = 10.0;
	}

	ServerReadiness ReadinessFrom(const ServerStatus& status)
	{
		switch (status.State)
		{
		case ServerState::Starting:
			return ServerReadiness::ComingUp;
		case ServerState::Indexing:
			return status.Loading.empty() ? ServerReadiness::Ready : ServerReadiness::ComingUp;
		case ServerState::Failed:
			return MayRestart(status.Restarts) ? ServerReadiness::ComingUp : ServerReadiness::CannotRun;
		case ServerState::NotFound:
		case ServerState::NotInstalled:
		case ServerState::MissingRuntime:
		case ServerState::Restricted:
		case ServerState::Off:
			return ServerReadiness::CannotRun;
		case ServerState::Ready:
		case ServerState::ProjectIncomplete:
		case ServerState::NotConfigured:
			break;
		}

		return ServerReadiness::Ready;
	}

	WorkspaceAnalysis::WorkspaceAnalysis(WorkspaceAnalysisPort& port, FileTextSource& texts,
										 AnalysisCache& cache, AnalysisStore* store)
		: m_Port(port), m_Texts(texts), m_Cache(cache), m_Store(store)
	{
	}

	WorkspaceAnalysis::~WorkspaceAnalysis()
	{
		DropAll();
	}

	void WorkspaceAnalysis::SetEnabled(bool enabled)
	{
		if (m_Enabled == enabled)
		{
			return;
		}

		m_Enabled = enabled;

		if (!m_Enabled)
		{
			Clear();
		}
	}

	void WorkspaceAnalysis::SetConcurrency(std::size_t limit)
	{
		m_Limit = std::max<std::size_t>(kMinimumConcurrency, limit);
	}

	void WorkspaceAnalysis::SetTabWidth(std::size_t width)
	{
		if (width == 0)
		{
			return;
		}

		m_TabWidth = width;
	}

	void WorkspaceAnalysis::SetWalking(bool walking)
	{
		m_Walking = walking;
	}

	void WorkspaceAnalysis::Enqueue(std::vector<std::filesystem::path> paths)
	{
		if (!m_Enabled)
		{
			return;
		}

		for (std::filesystem::path& path : paths)
		{
			m_Queues[LanguageOfPath(path)].push_back(std::move(path));
			++m_Queued;
		}
	}

	void WorkspaceAnalysis::Clear()
	{
		DropAll();

		m_Queues.clear();
		m_AwaitingText.clear();
		m_Arrived.clear();
		m_Done = 0;
		m_Queued = 0;
	}

	bool WorkspaceAnalysis::IsOutstanding(const std::filesystem::path& path) const
	{
		for (const PendingAnalysis& pending : m_Pending)
		{
			if (pending.Path == path)
			{
				return true;
			}
		}

		return std::find(m_AwaitingText.begin(), m_AwaitingText.end(), path) !=
			   m_AwaitingText.end();
	}

	void WorkspaceAnalysis::Drop(std::size_t index)
	{
		PendingAnalysis& pending = m_Pending[index];

		// WARNING: Closed even when a tab or a preview holds the document too: the port lets go of the pass's own hold,
		// and the document stays open for as long as anybody else holds it.
		if (pending.Opened)
		{
			m_Port.Close(pending.Path, pending.Language);
		}

		const std::vector<PendingAnalysis>::iterator droppedEntry =
			m_Pending.begin() + static_cast<std::ptrdiff_t>(index);

		m_Pending.erase(droppedEntry);
	}

	void WorkspaceAnalysis::DropAll()
	{
		while (!m_Pending.empty())
		{
			const std::size_t lastIndex = m_Pending.size() - 1;

			Drop(lastIndex);
		}
	}

	void WorkspaceAnalysis::Tick(
		double now, bool foregroundBusy,
		const std::function<bool(const std::filesystem::path&)>& heldElsewhere)
	{
		if (!m_Enabled)
		{
			return;
		}

		m_Pause = AnalysisPause::NotPaused;

		BeginArrivedFiles(now, heldElsewhere);

		for (PendingAnalysis& pending : m_Pending)
		{
			pending.HeldElsewhereAtAnyTick |= heldElsewhere(pending.Path);
		}

		FinishCompleted(now);

		if (!foregroundBusy)
		{
			m_BusySince.reset();
		}
		else if (!m_BusySince)
		{
			m_BusySince = now;
		}

		if (foregroundBusy)
		{
			const double busySeconds = now - *m_BusySince;
			const bool yieldsToForeground = busySeconds <= kForegroundYieldCapSeconds;

			if (yieldsToForeground)
			{
				return;
			}
		}

		StartNext(heldElsewhere);
	}

	void WorkspaceAnalysis::StartNext(
		const std::function<bool(const std::filesystem::path&)>& heldElsewhere)
	{
		// WARNING: AnalysisStore::Has answers false for everything until its index is polled in,
		// so starting before then would analyse again every file the store already holds.
		if (m_Store != nullptr && !m_Store->IsReady())
		{
			m_Pause = AnalysisPause::WhileSavedAnalysisLoads;
			return;
		}

		for (auto& [language, queue] : m_Queues)
		{
			while (m_Pending.size() + m_AwaitingText.size() < m_Limit && !queue.empty())
			{
				const std::filesystem::path path = Core::NormalizePath(queue.front());
				const ServerReadiness readiness = m_Port.ReadinessOf(path, language);

				if (readiness == ServerReadiness::ComingUp)
				{
					PauseUntilServerIsReady(language);
					break;
				}

				queue.pop_front();

				if (readiness == ServerReadiness::CannotRun)
				{
					++m_Done;
					continue;
				}

				if (IsOutstanding(path))
				{
					++m_Done;
					continue;
				}

				const bool storeAlreadyHasPath = m_Store != nullptr && m_Store->Has(path);

				if (storeAlreadyHasPath)
				{
					++m_Done;
					continue;
				}

				if (heldElsewhere(path))
				{
					++m_Done;
					continue;
				}

				if (!m_Port.HasChosenServer(language))
				{
					++m_Done;
					continue;
				}

				m_AwaitingText.push_back(path);
				m_Texts.Request(path);
			}
		}
	}

	void WorkspaceAnalysis::BeginArrivedFiles(
		double now, const std::function<bool(const std::filesystem::path&)>& heldElsewhere)
	{
		m_Arrived.clear();
		m_Texts.Take(m_Arrived);

		for (FileText& arrivedText : m_Arrived)
		{
			const std::vector<std::filesystem::path>::iterator awaited =
				std::find(m_AwaitingText.begin(), m_AwaitingText.end(), arrivedText.Path);

			const bool readLeftOverFromClosedFolder = awaited == m_AwaitingText.end();

			if (readLeftOverFromClosedFolder)
			{
				continue;
			}

			m_AwaitingText.erase(awaited);

			if (!arrivedText.Readable || arrivedText.Text.empty())
			{
				++m_Done;
				continue;
			}

			// WARNING: A tab can open on this path while the read is in flight. Opening it now
			// would overwrite what LanguageServices remembers for that tab with the bytes on disk.
			if (heldElsewhere(arrivedText.Path))
			{
				++m_Done;
				continue;
			}

			const LanguageId language = LanguageOfPath(arrivedText.Path);

			if (!m_Port.HasChosenServer(language))
			{
				++m_Done;
				continue;
			}

			PendingAnalysis pending;
			pending.Path = arrivedText.Path;
			pending.Language = language;
			pending.ModifiedAt = arrivedText.ModifiedAt;
			pending.Snapshot = Core::TextBuffer{arrivedText.Text};
			pending.StartedAt = now;

			m_Port.Open(pending.Path, language, std::move(arrivedText.Text));
			pending.Opened = true;

			pending.SemanticTokensTicket = m_Port.AskSemanticTokens(pending.Path, language);
			pending.SymbolsTicket = m_Port.AskSymbols(pending.Path, language);

			m_Pending.push_back(std::move(pending));
		}

		m_Arrived.clear();
	}

	bool WorkspaceAnalysis::Apply(const LanguageServices::Answer& answer)
	{
		for (PendingAnalysis& pending : m_Pending)
		{
			if (pending.SemanticTokensTicket && *pending.SemanticTokensTicket == answer.For)
			{
				pending.HaveSemanticTokens = true;
				pending.SemanticTokensTicket.reset();

				if (!answer.Failed)
				{
					pending.Result.Semantic.Set(answer.SemanticTokens, pending.Snapshot,
												m_TabWidth);
				}

				return true;
			}

			if (pending.SymbolsTicket && *pending.SymbolsTicket == answer.For)
			{
				pending.HaveSymbols = true;
				pending.SymbolsTicket.reset();

				if (!answer.Failed)
				{
					pending.Result.Declarations = answer.Declarations;
				}

				return true;
			}
		}

		return false;
	}

	void WorkspaceAnalysis::FinishCompleted(double now)
	{
		for (std::size_t index = 0; index < m_Pending.size();)
		{
			PendingAnalysis& pending = m_Pending[index];

			if (pending.HaveSemanticTokens && pending.HaveSymbols)
			{
				const std::filesystem::path analysedPath = pending.Path;
				const bool documentModified = false;

				std::optional<CachedAnalysis> analysisToKeep;

				if (!pending.HeldElsewhereAtAnyTick)
				{
					analysisToKeep = AnalysisWorthKeeping(std::move(pending.Result),
														  documentModified, pending.ModifiedAt);
				}

				Drop(index);
				++m_Done;

				if (analysisToKeep)
				{
					if (m_Store != nullptr)
					{
						m_Store->Write(analysedPath, *analysisToKeep);
					}

					m_Cache.Put(analysedPath, std::move(*analysisToKeep));
				}

				continue;
			}

			const bool timedOut = now - pending.StartedAt > kAnalysisTimeoutSeconds;

			if (timedOut)
			{
				Drop(index);
				++m_Done;

				continue;
			}

			++index;
		}
	}

	void WorkspaceAnalysis::PauseUntilServerIsReady(LanguageId language)
	{
		const bool alreadyPausedForAnother = m_Pause == AnalysisPause::UntilServerIsReady;

		if (alreadyPausedForAnother)
		{
			return;
		}

		m_Pause = AnalysisPause::UntilServerIsReady;
		m_PausedForServer = m_Port.ServerNameOf(language);
	}

	AnalysisProgress WorkspaceAnalysis::Progress() const
	{
		AnalysisProgress progress;
		progress.Done = m_Done;
		progress.Total = m_Queued;
		progress.Walking = m_Walking;

		// WARNING: A pause holds back one language's queue only, so the pass reads as paused only while nothing of any
		// other language is being analysed.
		const bool nothingBeingAnalysed = m_Pending.empty() && m_AwaitingText.empty();
		progress.Pause = nothingBeingAnalysed ? m_Pause : AnalysisPause::NotPaused;

		if (progress.Pause == AnalysisPause::UntilServerIsReady)
		{
			progress.PausedForServer = m_PausedForServer;
		}

		return progress;
	}
}
