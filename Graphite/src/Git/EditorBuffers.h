#pragma once

#include <filesystem>
#include <vector>

namespace Graphite {

	// What the Git panel needs to know about, and do to, the documents the editor holds open.
	// A seam rather than a DocumentWindows reference: the panel decides what a checkout may
	// overwrite, and that has to be testable without an editor behind it.
	class EditorBuffers
	{
	public:
		virtual ~EditorBuffers() = default;

		// Absolute paths of open documents with unsaved edits under root.
		virtual std::vector<std::filesystem::path> ModifiedUnder(
			const std::filesystem::path& root) const = 0;

		// Saves those documents; false (and nothing further should happen) if any could not be
		// saved.
		virtual bool Save(const std::vector<std::filesystem::path>& paths) = 0;

		// Throws away their unsaved edits, reloading them from disk.
		virtual void Revert(const std::vector<std::filesystem::path>& paths) = 0;

		// A change to these files is about to be seen and came from the IDE: reload without
		// asking.
		virtual void ExpectExternalWrite(const std::vector<std::filesystem::path>& paths) = 0;
	};

}
