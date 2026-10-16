#include "corepch.h"

#include "Core/Text/DocumentStore.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>

namespace Core {

	std::shared_ptr<TextDocument> DocumentStore::Find(const std::filesystem::path& path) const
	{
		for (const Entry& entry : m_Entries)
		{
			if (entry.Path == path)
			{
				return entry.Document.lock();
			}
		}

		return nullptr;
	}

	Result<std::shared_ptr<TextDocument>> DocumentStore::Acquire(const std::filesystem::path& path)
	{
		ForgetReleased();

		if (std::shared_ptr<TextDocument> existing = Find(path))
		{
			return existing;
		}

		Result<TextDocument> loaded = TextDocument::Load(path);

		if (!loaded)
		{
			return loaded.GetError();
		}

		std::shared_ptr<TextDocument> document = std::make_shared<TextDocument>(std::move(loaded).GetValue());
		m_Entries.push_back(Entry{.Path = path, .Document = document});

		return document;
	}

	std::vector<std::shared_ptr<TextDocument>> DocumentStore::GetDocuments() const
	{
		std::vector<std::shared_ptr<TextDocument>> documents;

		for (const Entry& entry : m_Entries)
		{
			if (std::shared_ptr<TextDocument> document = entry.Document.lock())
			{
				documents.push_back(std::move(document));
			}
		}

		return documents;
	}

	void DocumentStore::RewritePaths(const std::filesystem::path& movedFrom, const std::filesystem::path& movedTo)
	{
		for (Entry& entry : m_Entries)
		{
			const std::optional<std::filesystem::path> relativePath = RelativeTo(entry.Path, movedFrom);

			if (!relativePath)
			{
				continue;
			}

			// `movedTo / "."` keeps a trailing dot that compares unequal to `movedTo`, and Find would
			// then miss the document.
			const bool entryIsTheMovedPath = *relativePath == ".";
			entry.Path = entryIsTheMovedPath ? movedTo : movedTo / *relativePath;

			if (std::shared_ptr<TextDocument> document = entry.Document.lock())
			{
				document->SetPath(entry.Path);
			}
		}
	}

	void DocumentStore::ForgetReleased()
	{
		std::erase_if(m_Entries, [](const Entry& entry) { return entry.Document.expired(); });
	}

	bool IsLastHolder(const std::shared_ptr<TextDocument>& document)
	{
		return document != nullptr && document.use_count() == 1;
	}
}
