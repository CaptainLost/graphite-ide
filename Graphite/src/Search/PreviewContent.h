#pragma once

#include "Core/Text/TextBuffer.h"

#include <filesystem>
#include <string>

namespace Core {

	class DocumentSet;
}

namespace Graphite {

	enum class PreviewState
	{
		// Nothing selected. Distinct from Unreadable: one is "you have not picked a hit yet",
		// the other is "you picked one and it is gone".
		Empty,

		Ready,

		// The file could not be read, or is not there any more. Not a failure — the tree is
		// being changed under us while we look at it.
		Unreadable,

		// Binary or too large. A hit cannot have come from such a file, so this reports what it
		// found rather than deciding what to do about it.
		Skipped,
	};

	// The file a preview is showing, and where its text came from.
	//
	// An open document's buffer wins over the file on disk: the search already reads unsaved
	// text, so a preview reading disk would show different content from the result row above
	// it, for the same hit.
	class PreviewContent
	{
	public:
		// Call once per frame with the selected hit's file. Cheap to repeat for the same path:
		// the disk read happens when the path changes, or when the file stops being open.
		void Show(const std::filesystem::path& file, const Core::DocumentSet& documents);

		void Clear();

		PreviewState GetState() const { return m_State; }

		const std::filesystem::path& GetPath() const { return m_Path; }

		// Empty unless the state is Unreadable or Skipped.
		const std::string& GetMessage() const { return m_Message; }

		// Null unless the state is Ready. May point into Core::DocumentSet, which reallocates
		// as tabs open and close — so it is only valid until the next call to anything that
		// can change that set, and is re-resolved by every Show.
		const Core::TextBuffer* GetBuffer() const { return m_Buffer; }

	private:
		void LoadFromDisk();

		std::filesystem::path m_Path;
		PreviewState m_State = PreviewState::Empty;
		std::string m_Message;

		// The copy this owns, used only when the file is not open. Held separately from
		// m_Buffer so that "is what we are showing our own copy?" is a pointer comparison.
		Core::TextBuffer m_Loaded;

		const Core::TextBuffer* m_Buffer = nullptr;
	};
}
