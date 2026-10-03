#include "Editor/DocumentBuffers.h"

#include "Editor/DocumentWindows.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/DocumentSet.h"

namespace Graphite {

	std::vector<std::filesystem::path> DocumentBuffers::ModifiedUnder(
		const std::filesystem::path& root) const
	{
		std::vector<std::filesystem::path> modified;

		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			const Core::OpenItem& item = m_Documents.At(index);

			if (item.Document && item.Document->IsModified() &&
				Core::IsSameOrWithin(item.Path, root))
			{
				modified.push_back(item.Path);
			}
		}

		return modified;
	}

	bool DocumentBuffers::Save(const std::vector<std::filesystem::path>& paths)
	{
		return m_Windows.SaveDocuments(paths);
	}

	void DocumentBuffers::Revert(const std::vector<std::filesystem::path>& paths)
	{
		m_Windows.RevertDocuments(paths);
	}

	void DocumentBuffers::ExpectExternalWrite(const std::vector<std::filesystem::path>& paths)
	{
		m_Windows.ExpectExternalWrite(paths);
	}

}
