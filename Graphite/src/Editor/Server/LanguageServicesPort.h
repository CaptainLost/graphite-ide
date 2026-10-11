#pragma once

#include "Editor/Server/DocumentServerPort.h"
#include "Editor/Server/PreviewSemantics.h"
#include "Language/Analysis/WorkspaceAnalysis.h"
#include "Language/Analysis/WorkspaceAnalysisCoordinator.h"
#include "Language/Server/LanguageServices.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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

		ServerReadiness ReadinessOf(const std::filesystem::path& path, LanguageId language) override
		{
			if (!m_Services.HasChosenServer(language))
			{
				return ServerReadiness::CannotRun;
			}

			return ReadinessFrom(m_Services.StatusFor(path, language));
		}

		bool HasChosenServer(LanguageId language) override { return m_Services.HasChosenServer(language); }

	private:
		LanguageServices& m_Services;
	};

	// The production DocumentServerPort: DocumentWindows talks to this, the router's tests talk to
	// a fake, and neither knows about the other. Every call is forwarded as it stands - the
	// policy, such as starting a server before opening a document with it, stays with the caller
	// that has always kept it.
	class DocumentServicesPort final : public DocumentServerPort
	{
	public:
		explicit DocumentServicesPort(LanguageServices& services) : m_Services(services) {}

		void EnsureStarted(const std::filesystem::path& path, LanguageId language) override
		{
			m_Services.EnsureStarted(path, language);
		}

		void OpenDocument(const std::filesystem::path& path, LanguageId language,
						  std::string text) override
		{
			m_Services.OpenDocument(path, language, std::move(text));
		}

		void ChangeDocument(const std::filesystem::path& path, LanguageId language,
							std::string text) override
		{
			m_Services.ChangeDocument(path, language, std::move(text));
		}

		void SaveDocument(const std::filesystem::path& path, LanguageId language) override
		{
			m_Services.SaveDocument(path, language);
		}

		void CloseDocument(const std::filesystem::path& path, LanguageId language) override
		{
			m_Services.CloseDocument(path, language);
		}

		ServerStatus StatusFor(const std::filesystem::path& path, LanguageId language) override
		{
			return m_Services.StatusFor(path, language);
		}

		bool HasChosenServer(LanguageId language) override { return m_Services.HasChosenServer(language); }

		bool OffersSwitchSourceHeader(LanguageId language) override
		{
			return m_Services.OffersSwitchSourceHeader(language);
		}

		LanguageServices::Ticket AskDefinition(const std::filesystem::path& path,
											   LanguageId language,
											   const FilePosition& where) override
		{
			return m_Services.AskDefinition(path, language, where);
		}

		LanguageServices::Ticket AskReferences(const std::filesystem::path& path,
											   LanguageId language,
											   const FilePosition& where) override
		{
			return m_Services.AskReferences(path, language, where);
		}

		LanguageServices::Ticket AskHover(const std::filesystem::path& path, LanguageId language,
										  const FilePosition& where) override
		{
			return m_Services.AskHover(path, language, where);
		}

		LanguageServices::Ticket AskSwitchSourceHeader(const std::filesystem::path& path,
													   LanguageId language) override
		{
			return m_Services.AskSwitchSourceHeader(path, language);
		}

		LanguageServices::Ticket AskSymbols(const std::filesystem::path& path,
											LanguageId language) override
		{
			return m_Services.AskSymbols(path, language);
		}

		LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path& path,
												   LanguageId language) override
		{
			return m_Services.AskSemanticTokens(path, language);
		}

		void Cancel(LanguageServices::Ticket ticket) override { m_Services.Cancel(ticket); }

		void TakeAnswers(std::vector<LanguageServices::Answer>& answers) override
		{
			m_Services.TakeAnswers(answers);
		}

	private:
		LanguageServices& m_Services;
	};

	// The production WorkspaceServersPort, beside the three above and for the same reason: the
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
