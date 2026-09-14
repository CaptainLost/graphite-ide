#include "Lang/Analysis/WorkspaceAnalysis.h"

#include "Lang/Registry/LanguageRegistry.h"
#include "Lang/Server/ServerRegistry.h"

#include <utility>

namespace Graphite {

	namespace {

		// How long a flight may go unanswered before it is given up on. Generous, because a server
		// indexing a large project can take a while to get to a question nobody is waiting for —
		// and finite, because a question that is never answered would otherwise hold the whole
		// queue behind it for the rest of the session.
		constexpr double kFlightTimeoutSeconds = 120.0;

		// How long the open document may claim to be waiting before this stops yielding to it.
		//
		// Not a guard against a busy editor: typing goes quiet between keystrokes and never gets
		// near this. It guards against a foreground question that will never be answered — a
		// language whose server is configured but whose binary is not installed leaves the active
		// document's symbol request retrying forever, so what the editor reports is permanently
		// busy and nothing here would ever launch again for the rest of the session. Long enough
		// that a server merely slow to index still gets the whole floor, short enough that the
		// broken case costs one wait rather than a session.
		constexpr double kForegroundYieldCapSeconds = 10.0;
	}

	WorkspaceAnalysis::WorkspaceAnalysis(WorkspaceAnalysisPort& port, FileTextSource& texts,
										 AnalysisCache& cache)
		: m_Port(port), m_Texts(texts), m_Cache(cache)
	{
	}

	WorkspaceAnalysis::~WorkspaceAnalysis()
	{
		CloseFlight();
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
			m_Queue.push_back(std::move(path));
			++m_Queued;
		}
	}

	void WorkspaceAnalysis::Clear()
	{
		CloseFlight();

		m_Queue.clear();
		m_Reading.reset();
		m_Arrived.clear();
		m_Done = 0;
		m_Queued = 0;
	}

	void WorkspaceAnalysis::CloseFlight()
	{
		if (!m_Flight)
		{
			return;
		}

		if (m_Flight->Opened && !m_Flight->TabTook)
		{
			// A tab that took the file over is now the one holding the document, and it opened it
			// exactly once: CloseDocument counts no references and LensFor never opens a document
			// twice, so a didClose from here would take the file away from that tab for good —
			// every didChange it sent afterwards would describe a document the server no longer
			// holds. The didOpen is disowned instead, the same move PreviewSemantics makes on the
			// same transition.
			m_Port.Close(m_Flight->Path, m_Flight->Language);
		}

		m_Flight.reset();
	}

	void WorkspaceAnalysis::Tick(double now, bool foregroundBusy,
								 const std::function<bool(const std::filesystem::path&)>& hasTab)
	{
		if (!m_Enabled)
		{
			return;
		}

		CollectTexts(now);

		// The one place the predicate is asked about a flight. Everything that tears one down
		// reads the answer off the flight instead — see Flight::TabTook.
		if (m_Flight)
		{
			m_Flight->TabTook = hasTab(m_Flight->Path);
		}

		// Before the busy check, deliberately: a flight that has already been answered costs the
		// server nothing more, and leaving it open until the editor goes quiet would hold a
		// document — and the whole queue behind it — for as long as someone keeps typing.
		Retire();

		if (m_Flight && now - m_Flight->StartedAt > kFlightTimeoutSeconds)
		{
			// Given up on rather than waited for. Whatever became of the question, the file is no
			// worse off than it was before this class existed, and the queue behind it is.
			CloseFlight();
			++m_Done;
		}

		if (!foregroundBusy)
		{
			// Reset on the first quiet frame, so an ordinary session — busy for a keystroke,
			// quiet, busy again — never accumulates towards the cap. Only an unbroken run of
			// busy frames counts.
			m_BusySince.reset();
		}
		else if (!m_BusySince)
		{
			m_BusySince = now;
		}

		if (foregroundBusy && now - *m_BusySince <= kForegroundYieldCapSeconds)
		{
			// Nothing new while the file on screen is waiting. What is already in the air is left
			// alone: cancelling gives back none of the work the server has already done, and its
			// answer still has somewhere useful to go.
			//
			// Past the cap the yield is dropped — see kForegroundYieldCapSeconds for the case it
			// exists for.
			return;
		}

		Launch(hasTab);
	}

	void WorkspaceAnalysis::Launch(const std::function<bool(const std::filesystem::path&)>& hasTab)
	{
		if (m_Flight || m_Reading)
		{
			return;
		}

		while (!m_Queue.empty())
		{
			std::filesystem::path path = std::move(m_Queue.front());
			m_Queue.pop_front();

			if (hasTab(path))
			{
				// DocumentWindows opens this file with the server itself and asks its own
				// questions about the buffer, which may already differ from what is on disk. The
				// skip happens here, before the read is asked for, so a file with a tab costs
				// neither a server nor a worker.
				++m_Done;
				continue;
			}

			m_Reading = path;
			m_Texts.Request(path);

			return;
		}
	}

	void WorkspaceAnalysis::CollectTexts(double now)
	{
		m_Arrived.clear();
		m_Texts.Take(m_Arrived);

		for (FileText& text : m_Arrived)
		{
			if (!m_Reading || *m_Reading != text.Path)
			{
				// A read left over from a folder that has since closed. Nothing to do with the
				// file being waited for now.
				continue;
			}

			m_Reading.reset();

			if (!text.Readable || text.Text.empty())
			{
				++m_Done;
				continue;
			}

			const LanguageId language = LanguageOfPath(text.Path);

			if (ServerConfigFor(language) == nullptr)
			{
				// Nothing serves this language, so there is nobody to ask. Counted as done rather
				// than left in the queue: it is as finished as it will ever be.
				++m_Done;
				continue;
			}

			Flight flight;
			flight.Path = text.Path;
			flight.Language = language;
			flight.ModifiedAt = text.ModifiedAt;
			flight.Snapshot = Core::TextBuffer{text.Text};

			// Stamped where the questions are asked, not where the read was: the timeout is about
			// how long a server has been silent, and the read before it took however long it took.
			flight.StartedAt = now;

			m_Port.Open(flight.Path, language, std::move(text.Text));
			flight.Opened = true;

			flight.Colours = m_Port.AskSemanticTokens(flight.Path, language);
			flight.Symbols = m_Port.AskSymbols(flight.Path, language);

			m_Flight = std::move(flight);
		}

		m_Arrived.clear();
	}

	bool WorkspaceAnalysis::Apply(const LanguageServices::Answer& answer)
	{
		if (!m_Flight)
		{
			return false;
		}

		if (m_Flight->Colours && *m_Flight->Colours == answer.For)
		{
			m_Flight->ColoursIn = true;
			m_Flight->Colours.reset();

			if (!answer.Failed)
			{
				// Against the snapshot, not against anything else: the answer's columns were
				// measured on the text that went out with the request.
				m_Flight->Result.Semantic.Set(answer.SemanticTokens, m_Flight->Snapshot,
											  m_TabWidth);
			}

			return true;
		}

		if (m_Flight->Symbols && *m_Flight->Symbols == answer.For)
		{
			m_Flight->SymbolsIn = true;
			m_Flight->Symbols.reset();

			if (!answer.Failed)
			{
				m_Flight->Result.Declarations = answer.Declarations;
			}

			return true;
		}

		return false;
	}

	void WorkspaceAnalysis::Retire()
	{
		if (!m_Flight || !m_Flight->ColoursIn || !m_Flight->SymbolsIn)
		{
			return;
		}

		const std::filesystem::path path = m_Flight->Path;

		// AnalysisWorthKeeping refuses an entry with nothing in it, which is what a failed flight
		// comes to. `documentModified` is false because this class never has a document: what it
		// measured its answers against is the file on disk, and the stamp beside them describes
		// exactly that. A file that acquired a tab keeps nothing — the tab's own buffer has moved
		// on from what went to the server.
		std::optional<CachedAnalysis> worth =
			m_Flight->TabTook
				? std::nullopt
				: AnalysisWorthKeeping(std::move(m_Flight->Result), false, m_Flight->ModifiedAt);

		CloseFlight();
		++m_Done;

		if (worth)
		{
			m_Cache.Put(path, std::move(*worth));
		}
	}

	AnalysisProgress WorkspaceAnalysis::Progress() const
	{
		AnalysisProgress progress;
		progress.Done = m_Done;
		progress.Total = m_Queued;
		progress.Walking = m_Walking;

		return progress;
	}
}
