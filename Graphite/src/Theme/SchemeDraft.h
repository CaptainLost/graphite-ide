#pragma once

#include "Theme/ColorScheme.h"
#include "UI/ToastEvent.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// The scheme the settings section is looking at, and every decision it can make about it.
	// Takes its directory rather than asking AppSettings for one, so the whole of this class can
	// be exercised against a temporary directory instead of the user's own themes directory.
	//
	// Holds the sparse ColorScheme, not the resolved one: what a person edits is the set of
	// overrides, and keeping it sparse here is what keeps it sparse in the file.
	class SchemeDraft
	{
	public:
		explicit SchemeDraft(std::filesystem::path directory);

		// The two built-ins first, then whatever is on disk. Cached: rebuilt when this class
		// changes the directory itself, not on every frame, because a directory walk per frame
		// would be a syscall storm behind a list nobody is looking at.
		const std::vector<std::string>& GetNames() const { return m_Names; }

		const std::string& GetSelected() const { return m_Selected; }

		// True when the selection is a built-in, which is the whole of the read-only rule.
		bool IsReadOnly() const;

		void Select(std::string_view name);

		const ResolvedScheme& GetPreview() const { return m_Preview; }

		bool IsOverridden(TokenKind kind) const;
		bool IsOverridden(EditorColor color) const;
		bool IsOverridden(ChromeColor color) const;
		bool IsOverridden(GitColor color) const;
		bool IsOverridden(SessionColor color) const;

		// No-ops on a built-in. Writing through a read-only selection is a caller's mistake, and
		// silently ignoring it beats leaving an edit that the next Select would discard anyway.
		void SetColor(TokenKind kind, const Core::Color& color);
		void SetColor(EditorColor which, const Core::Color& color);
		void SetColor(ChromeColor which, const Core::Color& color);
		void SetColor(GitColor which, const Core::Color& color);
		void SetColor(SessionColor which, const Core::Color& color);

		// Clears the override rather than assigning the base value. The two look identical on
		// screen and behave differently forever after: an assigned value stops tracking the base,
		// so a colour added in a later version would never reach this scheme.
		void Revert(TokenKind kind);
		void Revert(EditorColor which);
		void Revert(ChromeColor which);
		void Revert(GitColor which);
		void Revert(SessionColor which);

		bool Save();

		// Copies the selection under a fresh name and selects it. This is the only route from a
		// read-only built-in to something editable.
		bool Duplicate();

		// Why `name` cannot be the selected scheme's new name, or nothing when it can. The scheme's
		// own name is not counted as taken, so a change of case alone is a rename; its exact current
		// name has no problem but is no rename either.
		std::optional<std::string> NameProblem(std::string_view name) const;

		// Renames the selected scheme's file and keeps it selected under the new name. False, with
		// nothing changed, for a built-in, a name NameProblem rejects, or a rename that fails.
		bool RenameSelected(std::string_view name);

		// Deletes the selected scheme and falls back to its base, so the editor is never left
		// pointed at a file that is gone.
		bool DeleteSelected();

		// The toast for the last Save, Duplicate or DeleteSelected that failed, handed out once,
		// for the section to publish: the draft holds no event bus, and is tested without one. A
		// failed rename is not kept here - the rename dialog says why in place.
		std::optional<ToastEvent> TakeFailure();

	private:
		void Rebuild();
		void Resolve();
		void Fail(std::string title, std::string message);

		std::filesystem::path m_Directory;

		std::optional<ToastEvent> m_Failure;

		std::vector<std::string> m_Names;

		std::string m_Selected;

		ColorScheme m_Scheme;

		ResolvedScheme m_Preview;
	};

	// "dark" against a directory holding dark-copy.json yields "dark-copy-2". Exposed for its
	// own test: the alternative is discovering the collision when SaveScheme overwrites someone's
	// work.
	std::string UniqueSchemeName(const std::filesystem::path& directory, std::string_view base);
}
