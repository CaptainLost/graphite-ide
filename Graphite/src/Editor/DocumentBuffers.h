#pragma once

#include "Git/EditorBuffers.h"

#include <filesystem>
#include <vector>

namespace Core {

	class DocumentSet;

}

namespace Graphite {

	class DocumentWindows;

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
