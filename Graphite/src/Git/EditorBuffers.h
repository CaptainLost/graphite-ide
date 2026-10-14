#pragma once

#include <filesystem>
#include <vector>

namespace Graphite {

	// WARNING: Every path passed in or out is absolute. Open documents are matched by whole path,
	// lexically normalised and compared case- and separator-insensitively; a path with no open
	// document is skipped.
	class EditorBuffers
	{
	public:
		virtual ~EditorBuffers() = default;

		virtual std::vector<std::filesystem::path> ModifiedUnder(
			const std::filesystem::path& root) const = 0;

		// WARNING: False means some document could not be saved and the caller must abort what it
		// was saving for: the unsaved edits are still only in the editor. The implementation has
		// already reported the failed file in the status bar.
		virtual bool Save(const std::vector<std::filesystem::path>& paths) = 0;

		virtual void Revert(const std::vector<std::filesystem::path>& paths) = 0;

		virtual void ExpectExternalWrite(const std::vector<std::filesystem::path>& paths) = 0;
	};

}
