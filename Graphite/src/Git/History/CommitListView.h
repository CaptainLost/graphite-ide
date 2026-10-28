#pragma once

#include "Core/Base/Base.h"

#include "Git/History/CommitGraphView.h"
#include "Git/History/CommitRefs.h"
#include "Git/History/ForkSeparator.h"
#include "Theme/GitPalette.h"
#include "Git/History/GraphGeometry.h"

#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/GraphLayout.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Graphite {

	struct FileSelection
	{
		Core::Oid Commit;
		std::filesystem::path Path;
	};

	// WARNING: a selection survives or not by its commit, never by its file, because the diff naming the file may not have arrived yet.
	std::optional<FileSelection> SelectionSurviving(const std::optional<FileSelection>& selection,
													const Core::LogPage& page);

	std::optional<Core::Oid> CommitSelectionSurviving(const std::optional<Core::Oid>& commit,
													  const Core::LogPage& page);

	struct CommitAction
	{
		enum class Kind
		{
			Checkout,
			NewBranch,
			Copy
		};

		Kind Action = Kind::Checkout;
		Core::Oid Commit;
		std::string Text;
		std::string What;
	};

	class CommitListView
	{
	public:
		void Render(const Core::LogPage& page, const Core::CommitGraph& graph,
					const GitPalette& palette, const std::chrono::time_zone* zone,
					const std::function<Core::Ref<const Core::Diff>(const Core::Oid&)>& diffOf,
					const std::function<bool(const Core::Oid&)>& diffFailed, const CommitRefMap& refs,
					std::string_view highlight = {},
					std::string_view pathHighlight = {},
					const std::optional<ForkSeparator>& separator = std::nullopt);

		void KeepSelectionWithin(const Core::LogPage& page);

		void ClearSelection() { m_SelectedFile.reset(); }

		void ClearCommitSelection() { m_SelectedCommit.reset(); }

		bool IsExpanded(const Core::Oid& commit) const;
		void ToggleExpanded(const Core::Oid& commit);
		void SetExpanded(const Core::Oid& commit, bool expanded);

		// Selects the commit as a click on its row does, which lets go of the selected file.
		void SelectCommit(const Core::Oid& commit);

		void SelectFile(const FileSelection& file) { m_SelectedFile = file; }

		// Brings the commit's row into view on the next Render that draws a page holding it; a page
		// without it drops the request.
		void ScrollToCommit(const Core::Oid& commit) { m_ScrollTarget = commit; }

		std::optional<FileSelection> GetSelectedFile() const { return m_SelectedFile; }

		std::optional<Core::Oid> GetSelectedCommit() const { return m_SelectedCommit; }

		// WARNING: expansions and selections name commits of the old repository once a workspace opens or closes, so all of them are cleared.
		void Reset();

		std::optional<CommitAction> TakeCommitAction();

		void SetActionsEnabled(bool enabled) { m_ActionsEnabled = enabled; }

		// WARNING: The list stops this far above the bottom of its space, so a line drawn after it
		// stays in view rather than below the scrolled table.
		void SetRoomBelow(float height) { m_RoomBelow = height; }

		void SetEmptyText(std::string_view text) { m_EmptyText = text; }

	private:
		std::size_t FileRowCountUnder(
			const Core::LogPage& page, std::size_t commitIndex,
			const std::function<Core::Ref<const Core::Diff>(const Core::Oid&)>& diffOf) const;

		void RenderCommitRow(const Core::LogPage& page, const Core::CommitSummary& commit,
							 const GitPalette& palette, const CommitRefMap& refs,
							 std::string_view highlight, std::chrono::sys_seconds now,
							 const std::chrono::time_zone* zone, bool isEarlierHistory);

		void RenderSeparatorRow(const std::string& text);

		void RenderFileRow(const Core::CommitSummary& commit, std::size_t fileIndex,
						   const GitPalette& palette,
						   const std::function<Core::Ref<const Core::Diff>(const Core::Oid&)>& diffOf,
						   const std::function<bool(const Core::Oid&)>& diffFailed,
						   std::string_view pathHighlight);

		std::unordered_set<Core::Oid> m_Expanded;
		std::optional<FileSelection> m_SelectedFile;
		std::optional<Core::Oid> m_SelectedCommit;
		std::optional<Core::Oid> m_ScrollTarget;

		std::optional<CommitAction> m_CommitAction;
		bool m_ActionsEnabled = true;
		float m_RoomBelow = 0.0f;
		std::string m_EmptyText = "No commits yet.";
	};
}
