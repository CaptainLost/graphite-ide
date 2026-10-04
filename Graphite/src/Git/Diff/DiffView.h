#pragma once

#include "Core/Base/Base.h"

#include "Theme/Palettes.h"
#include "Git/Diff/DiffRows.h"
#include "Git/Diff/DiffSelection.h"
#include "Git/Diff/FileTexts.h"
#include "Theme/GitPalette.h"
#include "Git/Diff/SyntaxRuns.h"

#include "Core/Git/Diff.h"
#include "Core/ImGui/SelectionGesture.h"
#include "Core/Text/TextSelection.h"

#include <imgui.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Graphite {

	// Draws one file's diff. Holds the mode, the scope and the syntax toggle below, plus a small cache of
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
		// `selection` is the editor scheme's selection colour: the diff draws code in the
		// editor's palette, so a selection in it looks the way one does in the editor.
		void Render(const Core::FileDiff& file, const GitPalette& palette,
					const SyntaxPalette& syntax, ImU32 selection,
					const Core::Ref<const FileTexts>& texts, const FileTokens& tokens);

		void SetMode(DiffViewMode mode) { m_Mode = mode; }
		DiffViewMode GetMode() const { return m_Mode; }

		void SetScope(DiffScope scope) { m_Scope = scope; }
		DiffScope GetScope() const { return m_Scope; }

		void SetColourSyntax(bool colour) { m_ColourSyntax = colour; }
		bool IsSyntaxColoured() const { return m_ColourSyntax; }

		// Set by the pane that draws this view, when opening the diff as a tab is possible.
		// Empty inside a tab: a tab that could open itself again would make a second copy
		// of what the reader is already looking at. A member rather than a Render parameter
		// because it is set once by whoever owns this view and does not vary call to call,
		// the way the file and palette do every frame.
		std::function<void()> OnOpenAsTab;

		// Opens the file in the editor, given its path relative to the working directory —
		// the form a diff carries it in, which the owner joins onto the directory it knows.
		// Unset hides the button; so does a file the diff deletes, having nothing left on disk.
		std::function<void(const std::filesystem::path&)> OnOpenFile;

		// Dismisses the pane. Left unset inside a tab, the way OnOpenAsTab is: a tab is
		// closed by its own close button, and a second one inside it would be two answers to
		// the same gesture.
		std::function<void()> OnClose;

		// Called with the text the menu's Copy has already put on the clipboard, for the owner
		// to confirm with a toast; not for Ctrl+C, which is not confirmed. The view itself
		// reaches no event bus.
		std::function<void(std::string_view)> OnCopied;

	private:
		void RenderToolbar(const Core::FileDiff& file);
		void RenderUnified(const std::vector<DiffRow>& rows, const Core::FileDiff& file,
						   const GitPalette& palette, const SyntaxPalette& syntax,
						   ImU32 selection, const FileTokens& tokens);
		void RenderSideBySide(const std::vector<DiffPair>& pairs, const Core::FileDiff& file,
							  const GitPalette& palette, const SyntaxPalette& syntax,
							  ImU32 selection, const FileTokens& tokens);

		// The gestures over the region open now, given that frame's rows: called inside it,
		// after every row has been drawn.
		void SelectUnified(const std::vector<DiffRow>& rows, const Core::FileDiff& file,
						   const FileTokens& tokens, float startX, float startY,
						   float textOffset, float rowHeight);
		void SelectSideBySide(const std::vector<DiffPair>& pairs, const Core::FileDiff& file,
							  const FileTokens& tokens, float startX, float startY,
							  float textOffset, float rowHeight, float innerLeft,
							  float rightPaneX, float paneWidth);

		// Runs the gesture with the diff's options and reports a copy to OnCopied.
		void UpdateSelection(const Core::SelectionSource& source);

		// DiffContentHash of `file`, recomputed only when `file` is not the one hashed last
		// time; see m_HashedFile.
		std::size_t ContentHashOf(const Core::FileDiff& file);

		// Where the region about to open should be scrolled to, when this frame's layout is a
		// different one than last frame's: the line kept from last frame, found in `rows` and
		// put back as far below the top edge as it was. `startY` is where the region's first
		// row will sit, which it cannot be asked for until it has opened. Answers once per
		// switch; null every other frame, leaving the scroll to the reader.
		template<typename Row>
		std::optional<float> TakeRestoredScroll(std::span<const Row> rows, float startY);

		// Notes which line on screen in the region now open a switch should keep in place —
		// see RowToKeep for which — for TakeRestoredScroll to find again should the next frame
		// lay the file out differently.
		template<typename Row>
		void RememberKeptLine(std::span<const Row> rows, const Core::FileDiff& file, float startY);

		// Splits `texts` into raw lines via SplitTextLines — the same split LexAllLines lexes
		// each line of — but only when `texts` is a different object than the one already
		// cached; see the class comment. Never called with a null `texts`: Render clears the
		// cache itself when the owner hands it one, rather than leaving the previous file's
		// full text referenced until the next whole-file render happens to replace it.
		void RebuildLineCache(const Core::Ref<const FileTexts>& texts);

		DiffViewMode m_Mode = DiffViewMode::SideBySide;
		DiffScope m_Scope = DiffScope::Hunks;
		bool m_ColourSyntax = true;

		// What was drawn last frame: the mode, and the scope — which is not m_Scope alone,
		// since the whole file waits on the texts. Only a change in what was drawn
		// moves the reader, so that is what decides to put them back.
		struct Layout
		{
			DiffViewMode Mode = DiffViewMode::SideBySide;
			DiffScope Scope = DiffScope::Hunks;

			bool operator==(const Layout& other) const = default;
		};

		// The line a switch of layout keeps in place, as last frame had it, and how far below
		// the region's top edge its row sat — negative when it was scrolled partly out of sight. Kept with the
		// file's path, so a layout that changes because another file arrived does not scroll
		// that file to a line number that meant something only in the previous one.
		struct KeptLine
		{
			std::filesystem::path Path;
			DiffLineAnchor Anchor;
			float Offset = 0.0f;
		};

		Layout m_Layout;
		std::optional<KeptLine> m_KeptLine;
		bool m_RestoreKeptLine = false;

		// The height of one row last frame, in the diff's mono font, which is not pushed yet
		// when the scroll for this one has to be decided.
		float m_RowHeight = 0.0f;

		// Identity-compared against the texts Render is next called with, so the split below
		// is redone only when the file actually changed to a different one, not every frame.
		Core::Ref<const FileTexts> m_CachedTexts;
		std::vector<std::string_view> m_OldLines;
		std::vector<std::string_view> m_NewLines;

		// TextsContentHash of m_CachedTexts, zero without them. A working-tree diff brings a
		// new texts object on every landing, equal or not, and a selection outlives the ones
		// whose content is equal.
		std::size_t m_TextsHash = 0;

		// The widest line the side-by-side view has drawn so far, which is how far its shared
		// horizontal scrollbar reaches. Only the rows that have been on screen are measured,
		// because only those are laid out — see RenderSideBySide for why a frame's lag is the
		// right price for not walking the whole file every frame. It only grows, and the two
		// below are what it is grown for: when either changes, the rows are a different set
		// and the width starts again.
		float m_WidestSideLine = 0.0f;
		DiffMeasurement m_WidestFor;

		// What the side-by-side region measured across inside its own border and scrollbar
		// last frame. Only the scrollbar's range is set from it — that has to be
		// declared before the region opens, which is before anything can be measured — while
		// the split between the panes is measured afresh each frame and does not lag.
		float m_SideInnerWidth = 0.0f;

		// The reader's selection. Unified: lines are row ordinals. Side-by-side: lines are the
		// file's line numbers on m_SelectionSide, the half the press that started it landed in.
		Core::TextSelection m_Selection;
		Core::SelectionGesture m_Gesture;
		DiffSide m_SelectionSide = DiffSide::New;

		// What m_Selection was made over. Render clears the selection when this frame's
		// differs: another file, mode, scope or content leaves its lines meaning something else.
		std::optional<DiffSelectionContext> m_SelectionFor;

		// The file m_ContentHash was computed for, and its measurement — the address alone
		// could be reused by the next snapshot. Hashing reads every line, so it is done once
		// per file, not once per frame.
		const Core::FileDiff* m_HashedFile = nullptr;
		DiffMeasurement m_HashedShape;
		std::size_t m_ContentHash = 0;
	};
}
