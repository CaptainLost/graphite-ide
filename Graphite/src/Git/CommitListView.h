#pragma once

#include "Git/CommitGraphView.h"
#include "Git/GitPalette.h"
#include "Git/GraphGeometry.h"

#include "Core/Git/Commit.h"
#include "Core/Git/GraphLayout.h"

#include <cstddef>
#include <optional>

namespace Graphite {

	// A list of commits with the graph down its left edge. Used by the Git view, and by the
	// file and line histories in the next plan — which is why it takes a LogPage and a graph
	// rather than reaching for a model.
	class CommitListView
	{
	public:
		void Render(const Core::LogPage& page, const Core::CommitGraph& graph,
					const GitPalette& palette);

		// Empty when nothing is selected, or when the selected commit is no longer in the
		// page — a refresh can drop it. Returned by value: the widget's own selection can be
		// reassigned by the next click, and a pointer into it would silently start naming a
		// different commit, or dangle after SelectFirst resets it on an empty page.
		std::optional<Core::Oid> GetSelected() const { return m_Selected; }

		void SelectFirst(const Core::LogPage& page);

		// True on the first frame, after a page arrives, that finds the reader at the
		// bottom of an incomplete page. Latched on the page's own commit count rather than
		// merely consumed: consuming only stops one arming from being read twice, it does
		// not stop the widget arming again next frame while the reader is still sitting at
		// the bottom and the bigger page has not arrived yet. Latching on the count means a
		// caller that ignores a request will not be nagged — a caller that ignored one
		// request would ignore sixty, so a second ask carries no information.
		bool TakeLoadMoreRequest();

	private:
		std::optional<Core::Oid> m_Selected;
		bool m_LoadMoreRequested = false;

		// The page size the last load-more request was armed at. -1 so the very first
		// incomplete page (size 0 is impossible here, Render returns early on empty) can
		// still arm.
		std::ptrdiff_t m_LoadMoreRequestedAtCount = -1;
	};
}
