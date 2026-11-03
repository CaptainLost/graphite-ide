#pragma once

#include "Git/GitModel.h"

#include <cstdint>
#include <functional>

namespace Graphite {

	class EditorBuffers;
	struct ChangesShared;

	class WriteConsequences
	{
	public:
		using FailureReport = std::function<void(const GitModel::WriteFailure& failure)>;
		using FilesWritten = std::function<void()>;

		WriteConsequences(GitModel& model, EditorBuffers& editor, ChangesShared& draft, FailureReport reportFailure,
						  FilesWritten filesWritten);

		void Apply();

	private:
		GitModel& m_Model;
		EditorBuffers& m_Editor;
		ChangesShared& m_Draft;
		FailureReport m_ReportFailure;
		FilesWritten m_FilesWritten;
		std::uint64_t m_SeenWrites = 0;
	};
}
