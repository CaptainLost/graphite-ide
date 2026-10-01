#pragma once

#include "Git/EditorBuffers.h"

namespace Core {

	class DocumentSet;

}

namespace Graphite {

	class DocumentWindows;

	// EditorBuffers over the open documents. Reads the set directly; everything that writes or
	// reloads goes through DocumentWindows, which owns the bookkeeping around a reload (the
	// language server, lenses, the status message).
	class DocumentBuffers final : public EditorBuffers
	{
	public:
		DocumentBuffers(Core::DocumentSet& documents, DocumentWindows& windows)
			: m_Documents(documents), m_Windows(windows)
		{
		}

		std::vector<std::filesystem::path> ModifiedUnder(
			const std::filesystem::path& root) const override;

		bool Save(const std::vector<std::filesystem::path>& paths) override;

		void Revert(const std::vector<std::filesystem::path>& paths) override;

		void ExpectExternalWrite(const std::vector<std::filesystem::path>& paths) override;

	private:
		Core::DocumentSet& m_Documents;
		DocumentWindows& m_Windows;
	};

}
