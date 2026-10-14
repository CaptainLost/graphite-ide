#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"
#include "Language/Server/LanguageServices.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	class DocumentServerPort
	{
	public:
		virtual ~DocumentServerPort() = default;

		DocumentServerPort() = default;
		DocumentServerPort(const DocumentServerPort&) = delete;
		DocumentServerPort& operator=(const DocumentServerPort&) = delete;

		virtual void EnsureStarted(const std::filesystem::path& path, LanguageId language) = 0;
		virtual void OpenDocument(const std::filesystem::path& path, LanguageId language,
								  std::string text) = 0;
		virtual void ChangeDocument(const std::filesystem::path& path, LanguageId language,
									std::string text) = 0;
		virtual void SaveDocument(const std::filesystem::path& path, LanguageId language) = 0;
		virtual void CloseDocument(const std::filesystem::path& path, LanguageId language) = 0;

		virtual ServerStatus StatusFor(const std::filesystem::path& path, LanguageId language) = 0;

		virtual bool HasChosenServer(LanguageId language) = 0;
		virtual bool OffersSwitchSourceHeader(LanguageId language) = 0;

		virtual LanguageServices::Ticket AskDefinition(const std::filesystem::path& path,
													   LanguageId language,
													   const FilePosition& position) = 0;
		virtual LanguageServices::Ticket AskReferences(const std::filesystem::path& path,
													   LanguageId language,
													   const FilePosition& position) = 0;
		virtual LanguageServices::Ticket AskHover(const std::filesystem::path& path,
												  LanguageId language,
												  const FilePosition& position) = 0;
		virtual LanguageServices::Ticket AskSwitchSourceHeader(const std::filesystem::path& path,
															   LanguageId language) = 0;
		virtual LanguageServices::Ticket AskSymbols(const std::filesystem::path& path,
													LanguageId language) = 0;
		virtual LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path& path,
														   LanguageId language) = 0;

		virtual void Cancel(LanguageServices::Ticket ticket) = 0;

		virtual void TakeAnswers(std::vector<LanguageServices::Answer>& answers) = 0;

		virtual void TakeRecolour(std::vector<std::filesystem::path>& paths) = 0;
	};
}
