#pragma once

#include "Editor/PreviewSemantics.h"
#include "Lang/Analysis/WorkspaceAnalysis.h"
#include "Lang/Analysis/WorkspaceAnalysisCoordinator.h"
#include "Lang/Server/LanguageServices.h"

namespace Graphite {

	// The production PreviewServerPort: PreviewSemantics talks to this, the tests talk to a fake,
	// and neither knows about the other.
	class LanguageServicesPort final : public PreviewServerPort
	{
	public:
		explicit LanguageServicesPort(LanguageServices& services) : m_Services(services) {}

		void Open(const std::filesystem::path& path, LanguageId language,
				  std::string text) override
		{
			// EnsureStarted before OpenDocument, in that order — the same order and the same
			// reason DocumentWindows::LensFor uses: OpenDocument is a no-op while no server runs
			// for this root, and server startup is asynchronous, so a preview declared the
			// instant a folder opens would otherwise race it and lose. Kept here rather than in
			// PreviewSemantics so the policy lives in one place and the bookkeeping class stays
			// free of server-startup concerns.
			m_Services.EnsureStarted(path, language);
			m_Services.OpenDocument(path, language, std::move(text));
		}

		LanguageServices::Ticket Ask(const std::filesystem::path& path,
									 LanguageId language) override
		{
			return m_Services.AskSemanticTokens(path, language);
		}

		void Close(const std::filesystem::path& path, LanguageId language) override
		{
			m_Services.CloseDocument(path, language);
		}

	private:
		LanguageServices& m_Services;
	};

	// The production WorkspaceAnalysisPort, beside the preview one above and for the same reason:
	// the class it serves talks to this, the tests talk to a fake, and neither knows about the
	// other.
	class WorkspaceAnalysisServicesPort final : public WorkspaceAnalysisPort
	{
	public:
		explicit WorkspaceAnalysisServicesPort(LanguageServices& services) : m_Services(services) {}

		void Open(const std::filesystem::path& path, LanguageId language,
				  std::string text) override
		{
			// EnsureStarted before OpenDocument, in that order and for the reason the port above
			// gives: OpenDocument is a no-op while no server runs for this root, and a background
			// file reached before the server started would otherwise be told to nobody.
			m_Services.EnsureStarted(path, language);
			m_Services.OpenDocument(path, language, std::move(text));
		}

		LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path& path,
												   LanguageId language) override
		{
			return m_Services.AskSemanticTokens(path, language);
		}

		LanguageServices::Ticket AskSymbols(const std::filesystem::path& path,
											LanguageId language) override
		{
			return m_Services.AskSymbols(path, language);
		}

		void Close(const std::filesystem::path& path, LanguageId language) override
		{
			m_Services.CloseDocument(path, language);
		}

		bool IsServerReady(const std::filesystem::path& path, LanguageId language) override
		{
			const ServerState state = m_Services.StatusFor(path, language).State;

			// Everything except the two states a server passes through on its way up. Missing,
			// broken and without-a-compile-database are all answers rather than waits: they do not
			// improve, and what such a server says is the best this file will ever get. Reading it
			// this way round also means a language nobody has configured never holds the pass.
			return state != ServerState::Starting && state != ServerState::Indexing;
		}

	private:
		LanguageServices& m_Services;
	};

	// The production WorkspaceServersPort, beside the two above and for the same reason: the
	// coordinator talks to this, the tests talk to a fake, and neither knows about the other.
	class LanguageServicesWorkspacePort final : public WorkspaceServersPort
	{
	public:
		explicit LanguageServicesWorkspacePort(LanguageServices& services) : m_Services(services) {}

		void Revalidate() override { m_Services.Revalidate(); }

		void WarmUp(const std::filesystem::path& root, bool respectGitIgnore) override
		{
			m_Services.WarmUp(root, respectGitIgnore);
		}

	private:
		LanguageServices& m_Services;
	};
}
