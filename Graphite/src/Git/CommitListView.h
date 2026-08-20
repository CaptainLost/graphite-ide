#pragma once

#include "Git/CommitGraphView.h"
#include "Git/GitPalette.h"
#include "Git/GraphGeometry.h"

#include "Core/Git/Commit.h"
#include "Core/Git/GraphLayout.h"

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

		// Null when nothing is selected, or when the selected commit is no longer in the
		// page — a refresh can drop it.
		const Core::Oid* GetSelected() const { return m_Selected ? &*m_Selected : nullptr; }

		void SelectFirst(const Core::LogPage& page);

		// True once, on the frame the reader asked for more history. Consumed rather than
		// polled, so one scroll to the bottom cannot queue a request every frame.
		bool TakeLoadMoreRequest();

	private:
		std::optional<Core::Oid> m_Selected;
		bool m_LoadMoreRequested = false;
	};
}
