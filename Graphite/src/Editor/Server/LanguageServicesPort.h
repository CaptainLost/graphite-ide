#pragma once

#include "Editor/Server/DocumentServerPort.h"
#include "Editor/Server/PreviewSemantics.h"
#include "Language/Analysis/WorkspaceAnalysis.h"
#include "Language/Analysis/WorkspaceAnalysisCoordinator.h"
#include "Language/Server/DocumentHolders.h"
#include "Language/Server/LanguageServices.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	// What DocumentHolders sends, to the language servers themselves.
	class LanguageServicesHeldPort final : public HeldDocumentPort
	{
	public:
		explicit LanguageServicesHeldPort(LanguageServices& services) : m_Services(services) {}

		void EnsureStarted(const std::filesystem::path& path, LanguageId language) override
		{
			m_Services.EnsureStarted(path, language);
		}

		void Open(const std::filesystem::path& path, LanguageId language, std::string text, DocumentRole role) override
		{
			m_Services.OpenDocument(path, language, std::move(text), role);
		}

		void Change(const std::filesystem::path& path, LanguageId language, std::string text) override
		{
			m_Services.ChangeDocument(path, language, std::move(text));
		}

		void Close(const std::filesystem::path& path, LanguageId language) override
		{
			m_Services.CloseDocument(path, language);
		}

	private:
		LanguageServices& m_Services;
	};

	class LanguageServicesPort final : public PreviewServerPort
	{
	public:
		LanguageServicesPort(LanguageServices& services, DocumentHolders& holders) : m_Services(services), m_Holders(holders)
		{
		}

		void Open(const std::filesystem::path& path, LanguageId, std::string text) override
		{
			m_Holders.Hold(DocumentHolder::Preview, path, std::move(text));
		}

		LanguageServices::Ticket Ask(const std::filesystem::path& path,
									 LanguageId language) override
		{
			return m_Services.AskSemanticTokens(path, language);
		}

		void Close(const std::filesystem::path& path, LanguageId) override
		{
			m_Holders.Release(DocumentHolder::Preview, path);
		}

	private:
		LanguageServices& m_Services;
		DocumentHolders& m_Holders;
	};

	class WorkspaceAnalysisServicesPort final : public WorkspaceAnalysisPort
	{
	public:
		WorkspaceAnalysisServicesPort(LanguageServices& services, DocumentHolders& holders)
			: m_Services(services), m_Holders(holders)
		{
		}

		void Open(const std::filesystem::path& path, LanguageId, std::string text) override
		{
			m_Holders.Hold(DocumentHolder::Analysis, path, std::move(text));
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

		void Close(const std::filesystem::path& path, LanguageId) override
		{
			m_Holders.Release(DocumentHolder::Analysis, path);
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

		std::string ServerNameOf(LanguageId language) override
		{
			const ServerDefinition* const chosen = m_Services.ChosenServer(language);

			return chosen != nullptr ? std::string{chosen->DisplayName} : std::string{};
		}

	private:
		LanguageServices& m_Services;
		DocumentHolders& m_Holders;
	};

	class DocumentServicesPort final : public DocumentServerPort
	{
	public:
		DocumentServicesPort(LanguageServices& services, DocumentHolders& holders) : m_Services(services), m_Holders(holders)
		{
		}

		void EnsureStarted(const std::filesystem::path& path, LanguageId language) override
		{
			m_Services.EnsureStarted(path, language);
		}

		void OpenDocument(const std::filesystem::path& path, LanguageId, std::string text) override
		{
			m_Holders.Hold(DocumentHolder::Tab, path, std::move(text));
		}

		void ChangeDocument(const std::filesystem::path& path, LanguageId, std::string text) override
		{
			m_Holders.Change(path, std::move(text));
		}

		void SaveDocument(const std::filesystem::path& path, LanguageId language) override
		{
			m_Services.SaveDocument(path, language);
		}

		void CloseDocument(const std::filesystem::path& path, LanguageId) override
		{
			m_Holders.Release(DocumentHolder::Tab, path);
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
											   const FilePosition& position) override
		{
			return m_Services.AskDefinition(path, language, position);
		}

		LanguageServices::Ticket AskReferences(const std::filesystem::path& path,
											   LanguageId language,
											   const FilePosition& position) override
		{
			return m_Services.AskReferences(path, language, position);
		}

		LanguageServices::Ticket AskHover(const std::filesystem::path& path, LanguageId language,
										  const FilePosition& position) override
		{
			return m_Services.AskHover(path, language, position);
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

		void TakeRecolour(std::vector<std::filesystem::path>& paths) override
		{
			m_Services.TakeRecolour(paths);
		}

	private:
		LanguageServices& m_Services;
		DocumentHolders& m_Holders;
	};

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
