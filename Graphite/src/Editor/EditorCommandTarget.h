#pragma once

#include "Editor/EditorCommands.h"

#include <filesystem>
#include <optional>

namespace Graphite {

	class ReferencesPanel;

	// What the editor commands act on: the editor view the user is working in. A view sets itself active when its window takes the keyboard.
	class EditorCommandTarget
	{
	public:
		virtual ~EditorCommandTarget() = default;
		EditorCommandTarget() = default;
		EditorCommandTarget(const EditorCommandTarget&) = delete;
		EditorCommandTarget& operator=(const EditorCommandTarget&) = delete;

		virtual bool CanRun(EditorAction action) = 0;
		virtual void Queue(EditorAction action, bool fromMenu) = 0;
		virtual bool CanSave() = 0;
		virtual void Save() = 0;
		virtual bool CanClose() = 0;
		virtual void Close() = 0;
		virtual bool CanFind() = 0;
		virtual void OpenFind() = 0;
		// The file the caret is in, which language commands act on.
		virtual std::optional<std::filesystem::path> CaretFile() = 0;
		virtual void GoToDefinition() = 0;
		virtual void FindReferences(ReferencesPanel& panel) = 0;
		// A line's history in a peek under it, only for a file whose lines HEAD can blame. From the context menu it is
		// the right-clicked line, otherwise the caret's.
		virtual bool CanShowLineHistory() = 0;
		virtual void ShowLineHistory(bool fromMenu) = 0;
	};

	class ActiveEditorTarget
	{
	public:
		void Set(EditorCommandTarget& target) { m_Target = &target; }
		// Null until a view has been focused.
		EditorCommandTarget* Get() const { return m_Target; }

	private:
		EditorCommandTarget* m_Target = nullptr;
	};
}
