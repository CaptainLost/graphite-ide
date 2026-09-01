#pragma once

#include "Core/Base/Base.h"

#include "Core/Command/Keymap.h"
#include "Core/Platform/ShellProfiles.h"
#include "Core/Term/TerminalSession.h"
#include "Core/UI/Panel.h"

#include "Panels/TerminalView.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Graphite {

	class Workspace;
	class ActiveScheme;

	// The shells running in this window, one per tab. The panel owns the sessions rather than the
	// views, because a session outlives the frames its view is drawn in: a build in a tab behind
	// another one keeps making progress, which is why every session is pumped and not just the
	// visible one.
	class TerminalPanel final : public Core::Panel
	{
	public:
		TerminalPanel(Workspace& workspace, const ActiveScheme& scheme, const Core::Keymap& keys);

		void OnUpdate(Core::Timestep ts) override;
		void OnImGuiRender() override;

	private:
		struct Tab
		{
			Core::Scope<Core::TerminalSession> Session;
			Core::Scope<TerminalView> View;
			std::string Label;

			// Never reused, so ImGui keeps each tab's state with the session it belongs to even
			// as the shell renames itself and as tabs to the left of it close.
			std::uint64_t Id = 0;

			// Set by the tab's close button or its context menu, acted on after the tab bar:
			// the session cannot be destroyed while ImGui is still drawing the tab it is in.
			bool Closing = false;
		};

		void OpenTab(const Core::ShellProfile& profile);
		void RenderTabBar();
		void RenderNewTerminalRow();
		void RemoveClosedTabs();

		Workspace& m_Workspace;
		const ActiveScheme& m_Scheme;
		const Core::Keymap& m_Keys;
		std::vector<Core::ShellProfile> m_Shells;
		std::vector<Tab> m_Tabs;
		std::uint64_t m_NextId = 1;

		// Which profile the + above the tabs will start. Held here rather than in the
		// draw so a choice survives the frame that made it.
		std::size_t m_SelectedShell = 0;

		// The first draw opens the default shell; closing every tab afterwards leaves the panel
		// empty rather than starting another one behind the reader's back.
		bool m_OpenedFirstTab = false;
	};
}
