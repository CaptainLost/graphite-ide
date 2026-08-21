#pragma once

#include "Editor/Palettes.h"
#include "Git/DiffRows.h"
#include "Git/FileTexts.h"
#include "Git/GitPalette.h"
#include "Git/SyntaxRuns.h"

#include "Core/Git/Diff.h"

#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace Graphite {

	enum class DiffViewMode
	{
		SideBySide,
		Unified
	};

	// Both sides' tokens for one file's texts, one entry per line, indexed the same way the
	// texts themselves are: line number minus one. Held by the owner and rebuilt only when the
	// FileTexts they came from changes, never lexed inside Render itself — see the header
	// comment on LexFileText for why lexing here, once a frame, would be the mistake.
	struct FileTokens
	{
		std::vector<std::vector<Token>> Old;
		std::vector<std::vector<Token>> New;
	};

	// Draws one file's diff. Holds the mode and the two toggles below, plus a small cache of
	// the file's own lines split out of its texts — rebuilt only when the FileTexts pointer
	// changes, for the same reason the caller's token cache is: splitting a whole file's text
	// on every frame is the same wasted pass LexFileText's own header warns against, just for
	// raw lines instead of tokens. Everything else — the rows themselves — is rebuilt from the
	// file each frame, because the file comes from a snapshot that may be replaced between
	// frames and rows cached across that swap would point into a diff that is no longer on
	// screen.
	class DiffView
	{
	public:
		// `texts` is null while the whole-file texts are still in flight, after a failure, or
		// before the owner has asked at all — Render falls back to the hunks-only view in every
		// one of those cases, which needs no text and is what the reader already had. `tokens`
		// is whatever the owner has lexed for the current `texts`; an empty side means no lexer
		// for that language, or no text yet, and both draw as plain text.
		void Render(const Core::FileDiff& file, const GitPalette& palette,
					const SyntaxPalette& syntax, const std::shared_ptr<const FileTexts>& texts,
					const FileTokens& tokens);

		void SetMode(DiffViewMode mode) { m_Mode = mode; }
		DiffViewMode GetMode() const { return m_Mode; }

		void SetShowWholeFile(bool show) { m_ShowWholeFile = show; }
		bool GetShowWholeFile() const { return m_ShowWholeFile; }

		void SetColourSyntax(bool colour) { m_ColourSyntax = colour; }
		bool GetColourSyntax() const { return m_ColourSyntax; }

		// Set by the pane that draws this view, when opening the diff as a tab is possible.
		// Empty inside a tab: a tab that could open itself again would make a second copy
		// of what the reader is already looking at. A member rather than a Render parameter
		// because it is set once by whoever owns this view and does not vary call to call,
		// the way the file and palette do every frame.
		std::function<void()> OnOpenAsTab;

	private:
		void RenderToolbar(const Core::FileDiff& file);
		void RenderUnified(const std::vector<DiffRow>& rows, const Core::FileDiff& file,
							const GitPalette& palette, const SyntaxPalette& syntax,
							const FileTokens& tokens);
		void RenderSideBySide(const std::vector<DiffPair>& pairs, const Core::FileDiff& file,
							   const GitPalette& palette, const SyntaxPalette& syntax,
							   const FileTokens& tokens);

		// Splits `texts` into raw lines the same way LexAllLines does, but only when `texts`
		// is a different object than the one already cached — see the class comment.
		void RebuildLineCache(const std::shared_ptr<const FileTexts>& texts);

		// What a row draws, resolved once so RenderUnified and RenderSideBySide agree on it:
		// the text (absent for a row whose indices no longer fit, matching LineOf), and the
		// tokens that colour it (null when there is nothing to colour it with).
		struct ResolvedLine
		{
			std::optional<std::string_view> Text;
			const std::vector<Token>* Tokens = nullptr;
		};

		ResolvedLine Resolve(const Core::FileDiff& file, const DiffRow& row,
							 const FileTokens& tokens) const;

		DiffViewMode m_Mode = DiffViewMode::SideBySide;
		bool m_ShowWholeFile = false;
		bool m_ColourSyntax = true;

		// Identity-compared against the texts Render is next called with, so the split below
		// is redone only when the file actually changed to a different one, not every frame.
		std::shared_ptr<const FileTexts> m_CachedTexts;
		std::vector<std::string_view> m_OldLines;
		std::vector<std::string_view> m_NewLines;
	};
}
