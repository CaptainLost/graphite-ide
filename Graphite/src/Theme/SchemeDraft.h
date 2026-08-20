#pragma once

#include "Theme/ColorScheme.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// The scheme the settings section is looking at, and every decision it can make about it.
	// Takes its directory rather than asking AppSettings for one, so the whole of this class can
	// be exercised against a temporary directory instead of the file beside the executable.
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

		// No-ops on a built-in. Writing through a read-only selection is a caller's mistake, and
		// silently ignoring it beats leaving an edit that the next Select would discard anyway.
		void SetColor(TokenKind kind, const Core::Color& color);
		void SetColor(EditorColor which, const Core::Color& color);
		void SetColor(ChromeColor which, const Core::Color& color);

		// Clears the override rather than assigning the base value. The two look identical on
		// screen and behave differently forever after: an assigned value stops tracking the base,
		// so a colour added in a later version would never reach this scheme.
		void Revert(TokenKind kind);
		void Revert(EditorColor which);
		void Revert(ChromeColor which);

		bool Save();

		// Copies the selection under a fresh name and selects it. This is the only route from a
		// read-only built-in to something editable.
		bool Duplicate();

		// Deletes the selected scheme and falls back to its base, so the editor is never left
		// pointed at a file that is gone.
		bool DeleteSelected();

	private:
		void Rebuild();
		void Resolve();

		std::filesystem::path m_Directory;

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
