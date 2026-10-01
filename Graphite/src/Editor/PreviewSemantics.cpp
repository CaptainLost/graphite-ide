#include "Editor/PreviewSemantics.h"

#include "Lang/Registry/LanguageRegistry.h"

#include <utility>

namespace Graphite {

	namespace {

		// Function-local rather than a namespace-scope object, per ADR 0033 on complex
		// initialization of globals — the same shape SemanticOverlay uses for the same reason.
		const std::vector<SemanticSpan>& NoSpans()
		{
			static const std::vector<SemanticSpan> empty;

			return empty;
		}

		// The same two seconds DocumentWindows waits before asking a failed request again.
		constexpr double kRetrySeconds = 2.0;
	}

	PreviewSemantics::~PreviewSemantics()
	{
		for (auto& [path, entry] : m_Entries)
		{
			if (entry.Opened)
			{
				m_Port.Close(path, LanguageOfPath(path));
			}
		}
	}

	void PreviewSemantics::Ask(const std::filesystem::path& path, Entry& entry)
	{
		// A ticket already outstanding is not cancelled, only replaced: Apply drops any answer
		// whose ticket is not the current one, so the old request costs a comparison rather than
		// a round trip to cancel it.
		entry.Ticket = m_Port.Ask(path, LanguageOfPath(path));
		entry.NextAskAt = 0.0;
	}

	void PreviewSemantics::Declare(const std::filesystem::path& path,
								   const Core::TextBuffer& buffer, std::size_t tabWidth,
								   bool editorHolds)
	{
		const auto found = m_Entries.find(path);

		if (found == m_Entries.end())
		{
			Entry entry;
			entry.Snapshot = Core::TextBuffer{buffer.Materialize()};
			entry.TabWidth = tabWidth;
			entry.Revision = buffer.GetRevision();
			entry.EditorHolds = editorHolds;
			entry.LastDeclared = m_Frame;

			Entry& kept = m_Entries.emplace(path, std::move(entry)).first->second;

			if (!editorHolds)
			{
				kept.Opened = true;
				m_Port.Open(path, LanguageOfPath(path), kept.Snapshot.Materialize());
			}

			Ask(path, kept);
			return;
		}

		Entry& entry = found->second;

		entry.LastDeclared = m_Frame;

		// editorHolds can change under an entry that keeps being declared: a double-click opens
		// a tab over a file this class was previewing, or the tab it was riding on gets closed.
		// Both are reconciled before anything else below, because the staleness check that
		// follows reads EditorHolds and would otherwise judge the edit against who owned the
		// document a frame ago.
		bool transitioned = false;

		if (editorHolds && !entry.EditorHolds)
		{
			// A tab took over. It sent its own didOpen — LensFor's — so this class's didOpen, if
			// it sent one, now describes a document a tab also holds open; closing here would
			// pull the file out from under the tab, which is the bug this whole reconciliation
			// exists to fix. The redundant didOpen the tab just sent for a file already open is
			// left alone: fixing it means refcounting LanguageServices::OpenDocuments, which
			// reaches into the editor's own document lifetime and is out of scope here.
			entry.EditorHolds = true;
			entry.Opened = false;
			transitioned = true;
		}
		else if (!editorHolds && entry.EditorHolds)
		{
			// The tab that was carrying this document closed. PruneDocumentStates already sent its
			// didClose, so the server has no copy of the file left — this class has to open one
			// again before it can keep asking about it.
			entry.EditorHolds = false;
			entry.Opened = true;
			m_Port.Open(path, LanguageOfPath(path), buffer.Materialize());
			transitioned = true;
		}

		// Two things make the snapshot stop describing what is on screen: an edit to a document
		// a tab holds, and a tab width change, since the overlay's offsets are measured in
		// tab-expanded text. Both drop the colours rather than trying to salvage them — the editor
		// itself no longer shifts colours for an edit either, and a preview has no edit position
		// to shift them by even if it wanted to. A moment without semantic colours is cheap; a
		// colour on the wrong word is not. A
		// transition is a third reason: the snapshot the outstanding request (if any) was made
		// against may belong to the wrong side of the handoff, so it is retaken unconditionally
		// rather than trusted because the revision happens to match.
		const bool moved = entry.EditorHolds && buffer.GetRevision() != entry.Revision;

		if (!moved && !transitioned && tabWidth == entry.TabWidth)
		{
			return;
		}

		entry.Overlay.Clear();
		entry.Snapshot = Core::TextBuffer{buffer.Materialize()};
		entry.Revision = buffer.GetRevision();
		entry.TabWidth = tabWidth;

		Ask(path, entry);
	}

	bool PreviewSemantics::Holds(const std::filesystem::path& path) const
	{
		const auto found = m_Entries.find(path);

		// Opened, not merely present: an entry riding on a tab's didOpen is the tab's to close,
		// and claiming it here would tell a third party the file is spoken for by this class when
		// it is not.
		return found != m_Entries.end() && found->second.Opened;
	}

	const std::vector<SemanticSpan>& PreviewSemantics::SpansOn(const std::filesystem::path& path,
															   std::size_t line) const
	{
		const auto found = m_Entries.find(path);

		return found == m_Entries.end() ? NoSpans() : found->second.Overlay.SpansOn(line);
	}

	bool PreviewSemantics::Apply(const LanguageServices::Answer& answer)
	{
		for (auto& [path, entry] : m_Entries)
		{
			if (entry.Ticket != answer.For)
			{
				continue;
			}

			entry.Ticket.reset();

			if (answer.Failed)
			{
				// Not asked again on the spot: a server that keeps failing would collect one
				// request per frame. The same shape LspAnswerRouter::AskSymbolsIfDue uses for the
				// same reason.
				entry.NextAskAt = m_Now + kRetrySeconds;
				return true;
			}

			// Against the snapshot, not against anything the caller holds now: the answer's
			// columns were measured on the text that went out with the request.
			entry.Overlay.Set(answer.SemanticTokens, entry.Snapshot, entry.TabWidth);
			return true;
		}

		return false;
	}

	void PreviewSemantics::Tick(std::size_t frame, double now)
	{
		m_Frame = frame;
		m_Now = now;

		for (auto it = m_Entries.begin(); it != m_Entries.end();)
		{
			Entry& entry = it->second;

			// One frame of grace. A pane's selection can vanish for a single frame when its
			// groups are rebuilt, and closing on the spot would make that blink cost a full
			// re-parse of the translation unit when it comes back.
			if (entry.LastDeclared + 1 < frame)
			{
				if (entry.Opened)
				{
					m_Port.Close(it->first, LanguageOfPath(it->first));
				}

				it = m_Entries.erase(it);
				continue;
			}

			if (!entry.Ticket && entry.NextAskAt > 0.0 && now >= entry.NextAskAt)
			{
				Ask(it->first, entry);
			}

			++it;
		}
	}
}
