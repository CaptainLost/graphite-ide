#pragma once

#include "Core/Base/Base.h"

#include "Theme/Palettes.h"
#include "Git/Diff/DiffRows.h"
#include "Git/Diff/DiffSelection.h"
#include "Git/Diff/DiffViewOptions.h"
#include "Git/Diff/DiffWrap.h"
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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	struct WholeFileNote
	{
		std::string Text;
		bool Loading = false;

		friend bool operator==(const WholeFileNote&, const WholeFileNote&) = default;
	};

	std::optional<WholeFileNote> WholeFileNoteFor(const DiffViewOptions& options, bool textsHeld, const TextsWait& wait);

	class DiffView
	{
	public:
		// WARNING: Holds for the next Render only, so an owner sets it on every frame it draws.
		void SetTextsWait(TextsWait wait) { m_TextsWait = std::move(wait); }

		void Render(const Core::FileDiff& file, const GitPalette& palette,
					const SyntaxPalette& syntax, ImU32 selection,
					const Core::Ref<const FileTexts>& wholeFileTexts, const FileTokens& tokens,
					const DiffViewOptions& options);

		// WARNING: Left unset inside a tab, so a tab never offers to open a second copy of itself.
		std::function<void()> OnOpenAsTab;

		// WARNING: The path is relative to the working directory, as a diff carries it. Left unset,
		// or for a file the diff deletes, the button is hidden.
		std::function<void(const std::filesystem::path&)> OnOpenFile;

		// WARNING: Left unset inside a tab, which has its own close button.
		std::function<void()> OnClose;

		// WARNING: Called only for the menu's Copy, never for Ctrl+C.
		std::function<void(std::string_view)> OnCopied;

		// WARNING: A choice in the toolbar is handed here and not kept: the options come back through Render,
		// from the one place every diff reads them, so the next diff opened is drawn the same way.
		std::function<void(const DiffViewOptions&)> OnSetOptions;

	private:
		struct SideBySideContext;

		void RenderToolbar(const Core::FileDiff& file, const std::optional<WholeFileNote>& note);
		void RenderUnified(const std::vector<DiffRow>& rows, const Core::FileDiff& file,
						   const GitPalette& palette, const SyntaxPalette& syntax,
						   ImU32 selection, const FileTokens& tokens, bool wordWrap);
		void RenderSideBySide(const std::vector<DiffPair>& pairs, const Core::FileDiff& file,
							  const GitPalette& palette, const SyntaxPalette& syntax,
							  ImU32 selection, const FileTokens& tokens, bool wordWrap);

		float DrawPane(const SideBySideContext& context, DiffSide side, std::size_t firstRow,
					   std::size_t endRow, std::size_t firstLine, const ImVec2& stepPos,
					   float stepScreenY) const;

		template<typename Row>
		const DiffRowLines& RowLinesFor(std::span<const Row> rows, const Core::FileDiff& file,
										const FileTokens& tokens, std::size_t columns);

		DiffRow DrawnRow(const Core::FileDiff& file, const DiffRow& row) const;

		std::size_t LinesOfRow(const Core::FileDiff& file, const DiffRow& row, DiffSide side,
							   const FileTokens& tokens, std::size_t columns) const;

		std::size_t ScreenLinesOf(const DiffRow& row, const Core::FileDiff& file,
								  const FileTokens& tokens, std::size_t columns) const;
		std::size_t ScreenLinesOf(const DiffPair& pair, const Core::FileDiff& file,
								  const FileTokens& tokens, std::size_t columns) const;

		void SelectUnified(const std::vector<DiffRow>& rows, const DiffRowLines& lines,
						   const Core::FileDiff& file, const FileTokens& tokens, float startX,
						   float startY, float textOffset, float rowHeight, std::size_t columns);
		void SelectSideBySide(const std::vector<DiffPair>& pairs, const DiffRowLines& lines,
							  const Core::FileDiff& file, const FileTokens& tokens, float startX,
							  float startY, float textOffset, float rowHeight, float innerLeft,
							  float rightPaneX, float paneWidth, std::size_t columns);

		void UpdateSelection(const Core::SelectionSource& source, bool wordWrap);

		std::size_t ContentHashOf(const Core::FileDiff& file);

		bool IsKeptLineOf(const Core::FileDiff& file) const;

		template<typename Row>
		std::optional<float> TakeRestoredScroll(std::span<const Row> rows,
												const DiffRowLines& lines, float startY);

		template<typename Row>
		float ScrollToKeptLine(std::span<const Row> rows, const DiffRowLines& lines, float startY) const;

		template<typename Row>
		bool KeepLineAcrossRewrap(std::span<const Row> rows, const DiffRowLines& lines,
								  const Core::FileDiff& file, float startY, std::size_t columns,
								  std::size_t columnsLastFrame);

		template<typename Row>
		void RememberKeptLine(std::span<const Row> rows, const DiffRowLines& lines,
							  const Core::FileDiff& file, float startY);

		void RebuildLineCache(const Core::Ref<const FileTexts>& texts);
		void ClearLineCache();

		DiffViewOptions m_Options;

		TextsWait m_TextsWait;

		struct Layout
		{
			DiffViewMode Mode = DiffViewMode::SideBySide;
			DiffScope Scope = DiffScope::Hunks;
			bool WordWrap = false;

			bool operator==(const Layout& other) const = default;
		};

		struct KeptLine
		{
			std::filesystem::path Path;
			DiffLineAnchor Anchor;
			float OffsetBelowTop = 0.0f;
		};

		Layout m_DrawnLayout;
		std::optional<KeptLine> m_KeptLine;
		bool m_RestoreKeptLine = false;

		float m_RowHeightLastFrame = 0.0f;

		Core::Ref<const FileTexts> m_CachedTexts;
		std::vector<std::string_view> m_OldLines;
		std::vector<std::string_view> m_NewLines;

		std::size_t m_TextsHash = 0;

		// WARNING: Only ever grows while m_WidestLineMeasuredFor stays the same. Rebuilding it from
		// the rows on screen would shrink the scroll range as the reader scrolls past the long line,
		// and ImGui would clamp their position back to the left.
		float m_WidestSideLine = 0.0f;
		DiffMeasurement m_WidestLineMeasuredFor;

		float m_SideInnerWidthLastFrame = 0.0f;
		float m_UnifiedInnerWidthLastFrame = 0.0f;

		struct RowLinesKey
		{
			std::filesystem::path Path;
			DiffViewMode Mode = DiffViewMode::SideBySide;
			DiffScope Scope = DiffScope::Hunks;
			std::size_t ContentHash = 0;
			std::size_t TextsHash = 0;
			std::size_t Rows = 0;
			std::size_t Columns = 0;

			bool operator==(const RowLinesKey& other) const = default;
		};

		DiffRowLines m_RowLines;
		std::optional<RowLinesKey> m_RowLinesKey;

		Core::TextSelection m_Selection;
		Core::SelectionGesture m_Gesture;
		DiffSide m_SelectionSide = DiffSide::New;

		std::optional<DiffSelectionContext> m_SelectionContext;

		const Core::FileDiff* m_HashedFile = nullptr;
		DiffMeasurement m_HashedShape;
		std::size_t m_ContentHash = 0;
	};
}
