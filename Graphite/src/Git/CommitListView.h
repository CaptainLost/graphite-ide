#pragma once

#include "Git/CommitGraphView.h"
#include "Git/GitPalette.h"
#include "Git/GraphGeometry.h"

#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/GraphLayout.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>

namespace Graphite {

	// Which file is being shown below the list. Carries the commit as well as the path,
	// because with several rows expanded the same path can appear under more than one.
	struct FileSelection
	{
		Core::Oid Commit;
		std::filesystem::path Path;
	};

	// A list of commits with the graph down its left edge. Used by the Git view, and by the
	// file and line histories in the next plan — which is why it takes a LogPage and a graph
	// rather than reaching for a model.
	class CommitListView
	{
	public:
		// `diffOf` answers a commit's diff, or null while it is being fetched. The list
		// asks for every commit it draws; the model is what decides whether that costs a
		// query.
		void Render(const Core::LogPage& page, const Core::CommitGraph& graph,
					const GitPalette& palette,
					const std::function<std::shared_ptr<const Core::Diff>(const Core::Oid&)>& diffOf);

		bool IsExpanded(const Core::Oid& commit) const;
		void ToggleExpanded(const Core::Oid& commit);

		std::optional<FileSelection> GetSelectedFile() const { return m_SelectedFile; }

		// True on the first frame, after a page arrives, that finds the reader at the
		// bottom of an incomplete page. Latched on the page's own commit count rather than
		// merely consumed: consuming only stops one arming from being read twice, it does
		// not stop the widget arming again next frame while the reader is still sitting at
		// the bottom and the bigger page has not arrived yet. Latching on the count means a
		// caller that ignores a request will not be nagged — a caller that ignored one
		// request would ignore sixty, so a second ask carries no information.
		bool TakeLoadMoreRequest();

	private:
		std::unordered_set<Core::Oid> m_Expanded;
		std::optional<FileSelection> m_SelectedFile;

		bool m_LoadMoreRequested = false;

		// The page size the last load-more request was armed at. -1 so the very first
		// incomplete page (size 0 is impossible here, Render returns early on empty) can
		// still arm.
		std::ptrdiff_t m_LoadMoreRequestedAtCount = -1;
	};
}
