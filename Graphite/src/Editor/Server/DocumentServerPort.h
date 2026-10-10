#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"
#include "Language/Server/LanguageServices.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	// What DocumentWindows, and the LspAnswerRouter inside it, need from the language servers and
	// nothing more.
	//
	// Separate from LanguageServices for the reason PreviewServerPort and WorkspaceAnalysisPort
	// are: which ticket supersedes which, when a failed one is asked again and what a restart
	// re-asks is bookkeeping worth asserting without a clangd process. The production
	// implementation is DocumentServicesPort in LanguageServicesPort.h; the tests hand the router
	// a port that records what it was asked and answers when told to.
	class DocumentServerPort
	{
	public:
		virtual ~DocumentServerPort() = default;

		DocumentServerPort() = default;
		DocumentServerPort(const DocumentServerPort&) = delete;
		DocumentServerPort& operator=(const DocumentServerPort&) = delete;

		// The document's life with its server: see the LanguageServices methods of the same names.
		virtual void EnsureStarted(const std::filesystem::path& path, LanguageId language) = 0;
		virtual void OpenDocument(const std::filesystem::path& path, LanguageId language,
								  std::string text) = 0;
		virtual void ChangeDocument(const std::filesystem::path& path, LanguageId language,
									std::string text) = 0;
		virtual void SaveDocument(const std::filesystem::path& path, LanguageId language) = 0;
		virtual void CloseDocument(const std::filesystem::path& path, LanguageId language) = 0;

		// The status of the server for this document's own (language, root).
		virtual ServerStatus StatusFor(const std::filesystem::path& path, LanguageId language) = 0;

		virtual bool HasChosenServer(LanguageId language) = 0;
		virtual bool OffersSwitchSourceHeader(LanguageId language) = 0;

		virtual LanguageServices::Ticket AskDefinition(const std::filesystem::path& path,
													   LanguageId language,
													   const FilePosition& where) = 0;
		virtual LanguageServices::Ticket AskReferences(const std::filesystem::path& path,
													   LanguageId language,
													   const FilePosition& where) = 0;
		virtual LanguageServices::Ticket AskHover(const std::filesystem::path& path,
												  LanguageId language,
												  const FilePosition& where) = 0;
		virtual LanguageServices::Ticket AskSwitchSourceHeader(const std::filesystem::path& path,
															   LanguageId language) = 0;
		virtual LanguageServices::Ticket AskSymbols(const std::filesystem::path& path,
													LanguageId language) = 0;
		virtual LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path& path,
														   LanguageId language) = 0;

		virtual void Cancel(LanguageServices::Ticket ticket) = 0;

		// Everything answered since the last call. The single drain point: see
		// LanguageServices::TakeAnswers.
		virtual void TakeAnswers(std::vector<LanguageServices::Answer>& answers) = 0;
	};
}
