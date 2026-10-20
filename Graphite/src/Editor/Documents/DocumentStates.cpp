#include "Editor/Documents/DocumentStates.h"

#include "Core/App/Redraw.h"
#include "Editor/Server/DocumentServerPort.h"
#include "Editor/Server/LspAnswerRouter.h"
#include "Language/Diagnostics/DiagnosticsModel.h"
#include "Language/Registry/LanguageRegistry.h"

namespace Graphite {

	DocumentStates::DocumentStates(DocumentServerPort& server, LspAnswerRouter& router,
								   const DiagnosticsModel& diagnostics, ClockReader now)
		: m_Server(server), m_Router(router), m_Diagnostics(diagnostics), m_Now(std::move(now))
	{
	}

	DocumentLevelState* DocumentStates::Find(const std::filesystem::path& path)
	{
		const auto found = m_States.find(path);

		return found != m_States.end() ? &found->second : nullptr;
	}

	SyntaxCache& DocumentStates::SyntaxFor(const std::filesystem::path& path, std::size_t tabWidth)
	{
		std::optional<SyntaxCache>& syntax = m_States[path].Syntax;

		if (!syntax)
		{
			syntax.emplace();
			syntax->SetSyntax(SyntaxOf(LanguageOfPath(path)));
			syntax->SetTabWidth(tabWidth);
		}

		return *syntax;
	}

	SemanticOverlay& DocumentStates::SemanticFor(const std::filesystem::path& path)
	{
		return m_States[path].Semantic;
	}

	DiagnosticOverlay& DocumentStates::DiagnosticFor(const std::filesystem::path& path,
													 const Core::TextBuffer& buffer, std::size_t tabWidth)
	{
		DocumentLevelState& state = m_States[path];

		const std::map<std::filesystem::path, DiagnosticsModel::Entry>& entries = m_Diagnostics.GetDocuments();
		const std::uint64_t noGeneration = 0;
		const std::uint64_t generation = entries.contains(path) ? entries.at(path).Generation : noGeneration;
		const std::size_t revision = buffer.GetRevision();

		DiagnosticGuard& guard = state.DiagnosticsBuiltFrom;

		if (guard.Revision == revision && guard.TabWidth == tabWidth && guard.Generation == generation)
		{
			return state.Diagnostics;
		}

		guard = DiagnosticGuard{.Revision = revision, .TabWidth = tabWidth, .Generation = generation};
		state.Diagnostics.Set(m_Diagnostics.For(path), buffer, tabWidth);

		return state.Diagnostics;
	}

	bool DocumentStates::OpenWithServer(const std::filesystem::path& path, const Core::TextDocument& document)
	{
		const LanguageId language = LanguageOfPath(path);

		if (!LanguageHasServers(language))
		{
			return false;
		}

		DocumentLevelState& state = m_States[path];

		if (state.OpenWithServer)
		{
			return false;
		}

		m_Server.EnsureStarted(path, language);
		m_Server.OpenDocument(path, language, document.GetBuffer().Materialize());
		m_Router.Track(path);

		state.OpenWithServer = true;
		state.SentChange = document.GetBuffer().GetChangeSequence();

		return true;
	}

	bool DocumentStates::IsOpenWithServer(const std::filesystem::path& path) const
	{
		const auto found = m_States.find(path);

		return found != m_States.end() && found->second.OpenWithServer;
	}

	bool DocumentStates::SendChangeIfMoved(const std::filesystem::path& path, const Core::TextDocument& document,
										   SendTiming timing)
	{
		DocumentLevelState* const state = Find(path);
		const std::uint64_t sequence = document.GetBuffer().GetChangeSequence();

		if (state == nullptr || !state->OpenWithServer || state->SentChange == sequence)
		{
			return false;
		}

		const Clock::time_point now = m_Now();

		if (state->NoticedChange != sequence)
		{
			state->NoticedChange = sequence;
			state->NoticedAt = now;
		}

		const bool isLarge = document.GetBuffer().GetLength() > kLargeDocumentBytes;
		const Clock::time_point quietFrom = state->NoticedAt + kLargeDocumentQuiet;

		if (timing == SendTiming::WhenQuiet && isLarge && now < quietFrom)
		{
			// WARNING: A redraw deadline lasts one wait, so each frame that finds the document still
			// settling has to ask for the end of the wait again.
			Core::Redraw::RequestIn(std::chrono::duration<double>(quietFrom - now));
			return false;
		}

		m_Server.ChangeDocument(path, LanguageOfPath(path), document.GetBuffer().Materialize());
		m_Router.WantSymbolsNow(path);
		state->SentChange = sequence;

		return true;
	}

	std::vector<std::filesystem::path> DocumentStates::SendChanges(const Core::DocumentStore& store)
	{
		std::vector<std::filesystem::path> sent;

		for (const auto& [path, state] : m_States)
		{
			if (!state.OpenWithServer)
			{
				continue;
			}

			const std::shared_ptr<Core::TextDocument> document = store.Find(path);

			if (document && SendChangeIfMoved(path, *document))
			{
				sent.push_back(path);
			}
		}

		return sent;
	}

	void DocumentStates::NoteSaved(const std::filesystem::path& path)
	{
		m_Server.SaveDocument(path, LanguageOfPath(path));
		m_Router.WantSymbolsNow(path);
	}

	void DocumentStates::Prune(const Core::DocumentStore& store)
	{
		for (auto entry = m_States.begin(); entry != m_States.end();)
		{
			if (store.Find(entry->first) != nullptr)
			{
				++entry;
				continue;
			}

			if (entry->second.OpenWithServer)
			{
				m_Server.CloseDocument(entry->first, LanguageOfPath(entry->first));
				m_Router.Forget(entry->first);
			}

			entry = m_States.erase(entry);
		}
	}
}
