#include "Language/Analysis/WorkspaceAnalysis.h"

#include "Language/Analysis/AnalysisStore.h"
#include "Language/Registry/LanguageRegistry.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <utility>

namespace Graphite {

	namespace {

		// How long a pending analysis may go unanswered before it is given up on. Generous,
		// because a server indexing a large project can take a while to get to a question nobody
		// is waiting for — and finite, because a question that is never answered would otherwise
		// hold its slot, and the queue behind it, for the rest of the session.
		constexpr double kAnalysisTimeoutSeconds = 120.0;

		// How long the open document may claim to be waiting before this stops yielding to it.
		//
		// Not a guard against a busy editor: typing goes quiet between keystrokes and never gets
		// near this. It guards against a foreground question that will never be answered — a
		// language whose server is configured but whose binary is not installed leaves the active
		// document's symbol request retrying forever, so what the editor reports is permanently
		// busy and nothing here would ever start again for the rest of the session. Long enough
		// that a server merely slow to index still gets the whole floor, short enough that the
		// broken case costs one wait rather than a session.
		constexpr double kForegroundYieldCapSeconds = 10.0;

		// How long to wait for a server to finish coming up before asking it anyway.
		//
		// Asking one that is still starting buys a question it does not answer, and the file is
		// then given up on at kAnalysisTimeoutSeconds rather than analysed at all — so a cold
		// clangd used to cost the first files of a session and two minutes of a counter sitting
		// at zero while nothing anywhere was busy. Waiting costs a few seconds once; not waiting
		// costs the timeout, every session, on the files most likely to be opened first.
		//
		// Bounded for the same reason the yield above is: a server configured but never installed
		// reports itself unready for the rest of the session, and an unbounded wait would switch
		// the feature off on exactly the machine where nobody would think to look for it.
		constexpr double kServerWaitCapSeconds = 60.0;
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
		// Lowering it starts nothing new and tears nothing down: what is already outstanding was
		// paid for when it started, and cancelling gives none of that back. StartNext simply finds
		// no room until enough entries have finished.
		m_Limit = std::max<std::size_t>(1, limit);
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
		DropAll();

		m_Queue.clear();
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

		if (pending.Opened && !pending.HeldElsewhere)
		{
			// Somebody who took the file over — a tab, a preview pane — is now the one holding the
			// document, and they opened it exactly once: CloseDocument counts no references and
			// neither LensFor nor PreviewSemantics::Declare opens a document twice, so a didClose
			// from here would take the file away from that reader for good — every request it sent
			// afterwards would describe a document the server no longer holds. The didOpen is
			// disowned instead, the same move PreviewSemantics makes on the same transition.
			//
			// The live field, deliberately, not EverHeldElsewhere: a holder that has gone away
			// took its document with it, so there is nothing left to take away and the didOpen
			// made here is the only one outstanding. Closing it is right; disowning it would leak
			// a document entry for the rest of the session.
			m_Port.Close(pending.Path, pending.Language);
		}

		m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(index));
	}

	void WorkspaceAnalysis::DropAll()
	{
		// From the back, so each erase moves nothing and the index is always in range.
		while (!m_Pending.empty())
		{
			Drop(m_Pending.size() - 1);
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

		BeginArrivedFiles(now, heldElsewhere);

		// The one place the predicate is asked about a file already open with the server.
		// Everything that tears an entry down reads the answer off the entry instead — see
		// PendingAnalysis::HeldElsewhere.
		for (PendingAnalysis& pending : m_Pending)
		{
			const bool held = heldElsewhere(pending.Path);

			// Two fields from the one call, because two different questions are asked of it later.
			// Drop needs the live answer — whether a didClose would take the document away from
			// somebody still reading it is a question about now. FinishCompleted needs the sticky
			// one — whether the answers still describe the file on disk is a question about ever.
			// See PendingAnalysis::EverHeldElsewhere for what collapsing them back into one field
			// costs.
			pending.HeldElsewhere = held;
			pending.EverHeldElsewhere |= held;
		}

		// Before the busy check, deliberately: an entry that has already been answered costs the
		// server nothing more, and leaving it open until the editor goes quiet would hold a
		// document — and the slot, and the queue behind it — for as long as someone keeps typing.
		FinishCompleted(now);

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
			// Nothing new while the file on screen is waiting — no slot, not merely the next one.
			// What is already outstanding is left alone: cancelling gives back none of the work
			// the server has already done, and its answer still has somewhere useful to go.
			//
			// Past the cap the yield is dropped — see kForegroundYieldCapSeconds for the case it
			// exists for.
			return;
		}

		StartNext(now, heldElsewhere);
	}

	bool WorkspaceAnalysis::ServerHeldBack(double now, const std::filesystem::path& path)
	{
		if (m_Port.IsServerReady(path, LanguageOfPath(path)))
		{
			// Reset on the first ready frame, so a server that restarts mid-session gets the
			// whole wait again rather than whatever was left of an earlier one.
			m_ServerUnreadySince.reset();
			return false;
		}

		if (!m_ServerUnreadySince)
		{
			m_ServerUnreadySince = now;
		}

		return now - *m_ServerUnreadySince <= kServerWaitCapSeconds;
	}

	void WorkspaceAnalysis::StartNext(
		double now, const std::function<bool(const std::filesystem::path&)>& heldElsewhere)
	{
		if (m_Store != nullptr && !m_Store->IsReady())
		{
			// Nothing new until the store can answer. Has reports false for everything until its
			// index has been polled in, and a false there is indistinguishable from a file the
			// store has never seen — so every path dequeued while the index worker is still
			// reading would be read, opened and analysed again at full price. Not a correctness
			// question: a miss is only ever a miss. But a second launch being cheap is the whole
			// of what the store is for, and starting early spends exactly the files it exists to
			// skip. The queue keeps its place; the next frame asks again.
			//
			// The gate is here rather than around Tick because everything else in a tick must go
			// on running while the store warms up: an analysis already outstanding still has to
			// be finished with, and one that has gone silent still has to time out.
			return;
		}

		// Awaited reads count against the limit alongside pending analyses: a read on its way is a
		// slot already spoken for, and counting only what a server has been told about would let
		// the whole queue be read at once.
		while (m_Pending.size() + m_AwaitingText.size() < m_Limit && !m_Queue.empty())
		{
			// Normalised here, on the way in, so that everything downstream is comparing the same
			// spelling: IsOutstanding's equality, the store's lookup — which normalises its own
			// keys — and the path this class hands the server. Two spellings of one file reaching
			// the queue would otherwise both pass IsOutstanding and produce two didOpens for one
			// document entry, which is the failure ADR 0512 is about.
			const std::filesystem::path path = Core::NormalizePath(m_Queue.front());

			if (ServerHeldBack(now, path))
			{
				// Asked before the queue is popped, so waiting keeps the file rather than spending
				// it: the next frame asks the same question about the same path.
				//
				// Head of the queue only. With one configured language that is the whole queue;
				// with two it would hold a Python file behind a clangd still coming up, which is
				// worth revisiting when a second server exists rather than solving in advance.
				return;
			}

			m_Queue.pop_front();

			if (IsOutstanding(path))
			{
				// The same file reached the queue twice. Counted all the same: it was counted into
				// the total when it was queued, and a skip that did not count would leave the bar
				// permanently short of its own figure. See IsOutstanding for why it is skipped.
				++m_Done;
				continue;
			}

			if (m_Store != nullptr && m_Store->Has(path))
			{
				// A previous session already asked about this version of this file. The whole of
				// what makes a second launch cheap: neither a server nor a worker is troubled, and
				// the entry is read from the disk in the frame a document opens it.
				++m_Done;
				continue;
			}

			if (heldElsewhere(path))
			{
				// Whoever holds this file opened it with the server themselves and asks their own
				// questions about the buffer, which may already differ from what is on disk. The
				// skip happens here, before the read is asked for, so a file somebody already
				// holds costs neither a server nor a worker.
				++m_Done;
				continue;
			}

			m_AwaitingText.push_back(path);
			m_Texts.Request(path);
		}
	}

	void WorkspaceAnalysis::BeginArrivedFiles(
		double now, const std::function<bool(const std::filesystem::path&)>& heldElsewhere)
	{
		m_Arrived.clear();
		m_Texts.Take(m_Arrived);

		for (FileText& text : m_Arrived)
		{
			const std::vector<std::filesystem::path>::iterator awaited =
				std::find(m_AwaitingText.begin(), m_AwaitingText.end(), text.Path);

			if (awaited == m_AwaitingText.end())
			{
				// A read left over from a folder that has since closed. Nothing to do with the
				// files being waited for now.
				continue;
			}

			// The erase is also the second half of the one-path-one-analysis check: a path only
			// reaches m_AwaitingText by passing IsOutstanding, and it leaves the moment its text
			// is taken up, so a second text for the same path — a read outstanding across a Clear
			// and a re-Enqueue is the way to get one — finds nothing here and is dropped above
			// rather than opening the document a second time. See IsOutstanding and ADR 0512.
			//
			// In that case it is the older read that is taken up and the fresh one that is
			// dropped: the reads come back in the order they were asked for, so the stale text
			// arrives first and is what matches. Safe, and only wasteful — the stamp that comes
			// with it describes the text that was actually read, so an entry built from it is
			// refused later by exactly the comparison that exists for a file edited behind the
			// editor's back, rather than colouring anything wrongly.
			m_AwaitingText.erase(awaited);

			if (!text.Readable || text.Text.empty())
			{
				++m_Done;
				continue;
			}

			// Asked again, having been asked in StartNext: the read between the two is a round
			// trip through a worker, at least a frame and often many, and a tab can open on this
			// path in the meantime. Opening it now would overwrite what LanguageServices remembers
			// for that tab with the bytes on disk — invisible until the server dies and
			// RestartServer replays them, at which point the tab's unsaved buffer is silently the
			// disk version on the server's side until the next edit corrects it.
			if (heldElsewhere(text.Path))
			{
				++m_Done;
				continue;
			}

			const LanguageId language = LanguageOfPath(text.Path);

			if (!m_Port.HasChosenServer(language))
			{
				// Nothing serves this language, so there is nobody to ask. Counted as done rather
				// than left in the queue: it is as finished as it will ever be.
				++m_Done;
				continue;
			}

			PendingAnalysis pending;
			pending.Path = text.Path;
			pending.Language = language;
			pending.ModifiedAt = text.ModifiedAt;
			pending.Snapshot = Core::TextBuffer{text.Text};

			// Stamped where the questions are asked, not where the read was: the timeout is about
			// how long a server has been silent, and the read before it took however long it took.
			pending.StartedAt = now;

			m_Port.Open(pending.Path, language, std::move(text.Text));
			pending.Opened = true;

			pending.Colours = m_Port.AskSemanticTokens(pending.Path, language);
			pending.Symbols = m_Port.AskSymbols(pending.Path, language);

			m_Pending.push_back(std::move(pending));
		}

		m_Arrived.clear();
	}

	bool WorkspaceAnalysis::Apply(const LanguageServices::Answer& answer)
	{
		for (PendingAnalysis& pending : m_Pending)
		{
			if (pending.Colours && *pending.Colours == answer.For)
			{
				pending.HaveColours = true;
				pending.Colours.reset();

				if (!answer.Failed)
				{
					// Against the snapshot, not against anything else: the answer's columns were
					// measured on the text that went out with the request.
					pending.Result.Semantic.Set(answer.SemanticTokens, pending.Snapshot,
												m_TabWidth);
				}

				return true;
			}

			if (pending.Symbols && *pending.Symbols == answer.For)
			{
				pending.HaveSymbols = true;
				pending.Symbols.reset();

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
		// By index rather than by iterator: Drop erases from the middle, and an iterator held
		// across that is already invalid by the time the loop would use it again. After an erase
		// the next entry has moved into this index, so the index only advances when nothing was
		// removed.
		for (std::size_t index = 0; index < m_Pending.size();)
		{
			PendingAnalysis& pending = m_Pending[index];

			if (pending.HaveColours && pending.HaveSymbols)
			{
				const std::filesystem::path path = pending.Path;

				// AnalysisWorthKeeping refuses an entry with nothing in it, which is what a failed
				// analysis comes to. `documentModified` is false because this class never has a
				// document: what it measured its answers against is the file on disk, and the
				// stamp beside them describes exactly that. A file somebody else took over keeps
				// nothing — the buffer they hold has moved on from what went to the server.
				//
				// The sticky field, not the live one: a holder that has since gone away still
				// moved the server's picture of the file off the disk for as long as it existed,
				// and an answer measured while it was there says nothing about the bytes this
				// entry's stamp describes. See PendingAnalysis::EverHeldElsewhere.
				std::optional<CachedAnalysis> worth;

				if (!pending.EverHeldElsewhere)
				{
					worth = AnalysisWorthKeeping(std::move(pending.Result), false,
												 pending.ModifiedAt);
				}

				Drop(index);
				++m_Done;

				if (worth)
				{
					// Both tiers. The cache is this session's, the store is every session after
					// it, and a pass that filled only the first would leave the second launch
					// asking the server about a workspace nothing had changed.
					if (m_Store != nullptr)
					{
						m_Store->Write(path, *worth);
					}

					m_Cache.Put(path, std::move(*worth));
				}

				continue;
			}

			if (now - pending.StartedAt > kAnalysisTimeoutSeconds)
			{
				// Given up on rather than waited for. Whatever became of the question, the file is
				// no worse off than it was before this class existed, and the slot is free again
				// for one that might be answered.
				Drop(index);
				++m_Done;

				continue;
			}

			++index;
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
