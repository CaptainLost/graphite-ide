#pragma once

#include "Core/Git/Ref.h"
#include "Git/Window/GitWindowSelection.h"
#include "Git/Window/NavigatorRows.h"

#include <functional>
#include <vector>

namespace Graphite {

	class ActiveScheme;

	class NavigatorView
	{
	public:
		explicit NavigatorView(const ActiveScheme& scheme);

		std::function<void(const GitWindowSelection& selection)> OnSelect;
		std::function<void()> OnShowReady;
		std::function<void(const Core::Branch& branch)> DrawBranchMenu;

		// The rows only; GitView draws the header above and the stashes below. Call it inside a list body.
		void Draw(const std::vector<NavigatorRow>& rows, const std::vector<Core::Branch>& branches);

	private:
		const ActiveScheme& m_Scheme;
	};
}
