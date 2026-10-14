#include "Editor/Server/PreviewSemantics.h"

#include "Editor/Server/LspAnswerRouter.h"
#include "Language/Registry/LanguageRegistry.h"

#include "Core/Base/EmptyVector.h"

#include <utility>

namespace Graphite {

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
		entry.Ticket = m_Port.Ask(path, LanguageOfPath(path));
		entry.NextAskAt = 0.0;
	}

	void PreviewSemantics::Declare(const std::filesystem::path& path,
								   const Core::TextBuffer& buffer, std::size_t tabWidth,
								   bool editorHolds)
	{
		const EntryMap::iterator found = m_Entries.find(path);

		if (found == m_Entries.end())
		{
			Entry& kept = m_Entries[path];
			kept.Snapshot = Core::TextBuffer{buffer.Materialize()};
			kept.TabWidth = tabWidth;
			kept.Revision = buffer.GetRevision();
			kept.EditorHolds = editorHolds;
			kept.LastDeclared = m_Frame;

			if (!editorHolds)
			{
				kept.Opened = true;
				m_Port.Open(path, LanguageOfPath(path), kept.Snapshot.Materialize());
			}

			Ask(path, kept);
			return;
		}

		[[maybe_unused]] auto& [foundPath, entry] = *found;

		entry.LastDeclared = m_Frame;

		bool transitioned = false;
		const bool tabTookOver = editorHolds && !entry.EditorHolds;
		const bool tabClosed = !editorHolds && entry.EditorHolds;

		if (tabTookOver)
		{
			entry.EditorHolds = true;
			entry.Opened = false;
			transitioned = true;
		}
		else if (tabClosed)
		{
			entry.EditorHolds = false;
			entry.Opened = true;
			m_Port.Open(path, LanguageOfPath(path), buffer.Materialize());
			transitioned = true;
		}

		const bool editedUnderTab = entry.EditorHolds && buffer.GetRevision() != entry.Revision;
		const bool tabWidthChanged = tabWidth != entry.TabWidth;

		if (!editedUnderTab && !transitioned && !tabWidthChanged)
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
		const EntryMap::const_iterator found = m_Entries.find(path);

		if (found == m_Entries.end())
		{
			return false;
		}

		[[maybe_unused]] const auto& [foundPath, foundEntry] = *found;

		return foundEntry.Opened;
	}

	const std::vector<SemanticSpan>& PreviewSemantics::SpansOn(const std::filesystem::path& path,
															   std::size_t line) const
	{
		const EntryMap::const_iterator found = m_Entries.find(path);

		if (found == m_Entries.end())
		{
			return Core::SharedEmptyVector<SemanticSpan>();
		}

		[[maybe_unused]] const auto& [foundPath, foundEntry] = *found;

		return foundEntry.Overlay.SpansOn(line);
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
				entry.NextAskAt = m_Now + kFailedRequestRetrySeconds;
				return true;
			}

			entry.Overlay.Set(answer.SemanticTokens, entry.Snapshot, entry.TabWidth);
			return true;
		}

		return false;
	}

	void PreviewSemantics::Tick(std::size_t frame, double now)
	{
		m_Frame = frame;
		m_Now = now;

		for (EntryMap::iterator entryIterator = m_Entries.begin(); entryIterator != m_Entries.end();)
		{
			auto& [path, entry] = *entryIterator;

			const bool undeclaredForAWholeFrame = entry.LastDeclared + 1 < frame;

			if (undeclaredForAWholeFrame)
			{
				if (entry.Opened)
				{
					m_Port.Close(path, LanguageOfPath(path));
				}

				entryIterator = m_Entries.erase(entryIterator);
				continue;
			}

			const bool retryDue = !entry.Ticket && entry.NextAskAt > 0.0 && now >= entry.NextAskAt;

			if (retryDue)
			{
				Ask(path, entry);
			}

			++entryIterator;
		}
	}
}
