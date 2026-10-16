#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/TextDocument.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace Core {

	// The open documents, one per path, shared by every view that shows one. Views hold the
	// documents; the store only keeps weak references, so a document dies with its last view.
	class DocumentStore
	{
	public:
		// Matched exactly, as DocumentSet::Find matches a tab's path.
		std::shared_ptr<TextDocument> Find(const std::filesystem::path& path) const;

		// The registered document, or the file loaded and registered now.
		Result<std::shared_ptr<TextDocument>> Acquire(const std::filesystem::path& path);

		std::vector<std::shared_ptr<TextDocument>> GetDocuments() const;

		void RewritePaths(const std::filesystem::path& movedFrom, const std::filesystem::path& movedTo);

	private:
		struct Entry
		{
			std::filesystem::path Path;
			std::weak_ptr<TextDocument> Document;
		};

		void ForgetReleased();

		std::vector<Entry> m_Entries;
	};

	// Whether letting go of this pointer destroys the document: the one to ask about unsaved
	// changes before it does.
	bool IsLastHolder(const std::shared_ptr<TextDocument>& document);
}
