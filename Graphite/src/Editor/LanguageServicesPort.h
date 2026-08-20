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
}
