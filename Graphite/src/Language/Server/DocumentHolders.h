#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace Graphite {

	enum class DocumentHolder : std::uint8_t
	{
		Tab,
		Preview,
		Analysis
	};

	// The four calls DocumentHolders makes of the language servers.
	class HeldDocumentPort
	{
	public:
		virtual ~HeldDocumentPort() = default;

		virtual void EnsureStarted(const std::filesystem::path& path, LanguageId language) = 0;
		virtual void Open(const std::filesystem::path& path, LanguageId language, std::string text, DocumentRole role) = 0;
		virtual void Change(const std::filesystem::path& path, LanguageId language, std::string text) = 0;
		virtual void Close(const std::filesystem::path& path, LanguageId language) = 0;
	};

	// Who holds each document open with a language server: a tab, a preview, the background analysis. The first holder
	// opens it and the last to let go closes it, so no holder takes a document away from another. The tab's text is the
	// one the server keeps: a tab taking a document over sends its own text as a shown document, and a tab letting go
	// while others still hold it hands it back with the text they saw.
	class DocumentHolders
	{
	public:
		explicit DocumentHolders(HeldDocumentPort& server) : m_Server(server) {}

		// Holding again refreshes the server's text, unless a tab holds the document and its text stands.
		void Hold(DocumentHolder holder, const std::filesystem::path& path, std::string text);

		// Sent only while somebody holds the document.
		void Change(const std::filesystem::path& path, std::string text);

		void Release(DocumentHolder holder, const std::filesystem::path& path);

		bool IsHeld(const std::filesystem::path& path) const;
		bool IsHeldBy(DocumentHolder holder, const std::filesystem::path& path) const;
		bool IsHeldByAnyBut(DocumentHolder holder, const std::filesystem::path& path) const;

	private:
		static constexpr std::size_t kHolderCount = 3;

		struct HeldDocument
		{
			std::array<bool, kHolderCount> HeldBy{};

			// What the holders other than a tab last saw, for a tab that lets go while they still hold it.
			std::string ConsultedText;
		};

		static bool HasAnyHolder(const HeldDocument& document);

		HeldDocumentPort& m_Server;
		std::map<std::filesystem::path, HeldDocument> m_Documents;
	};
}
