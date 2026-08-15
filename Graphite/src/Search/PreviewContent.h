#pragma once

#include "Core/Text/TextBuffer.h"
#include "Lang/Syntax/SyntaxCache.h"
#include "Lang/Syntax/Token.h"

#include <filesystem>
#include <string>
#include <vector>

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

		// This preview's own colours, not the editor's. Borrowing the editor's would mean
		// handing this class a reference to DocumentWindows — a panel reaching into another
		// panel's internals to save one lexing pass.
		//
		// Empty for a file with no lexer, for a line past the end, and when nothing is showing.
		//
		// Not const: lexing fills the cache. A line's colours depend on every line above it, so
		// asking for line 400 lexes lines 0 to 400 — once per file, which is what the cache is
		// for.
		const std::vector<Token>& TokensOf(std::size_t line);

		// The editor's tab width, so the preview breaks columns where the editor would. Resets
		// the cache when it changes, because the lexer is fed tab-expanded text.
		void SetTabWidth(std::size_t width);

	private:
		void LoadFromDisk();

		std::filesystem::path m_Path;
		PreviewState m_State = PreviewState::Empty;
		std::string m_Message;

		// The copy this owns, used only when the file is not open. Held separately from
		// m_Buffer so that "is what we are showing our own copy?" is a pointer comparison.
		Core::TextBuffer m_Loaded;

		const Core::TextBuffer* m_Buffer = nullptr;

		SyntaxCache m_Syntax;
	};
}
