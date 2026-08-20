#pragma once

#include "Editor/PreviewSemantics.h"
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
}
