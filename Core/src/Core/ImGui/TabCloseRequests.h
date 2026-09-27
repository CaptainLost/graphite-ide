#pragma once

#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace Core {

	// Windows someone has asked to close from outside their owner - a tab's context menu,
	// for one. Nothing here closes a window: each owner takes its own name after Begin and
	// treats a hit as its close button having been clicked, so its own close path - the
	// question about unsaved changes included - is the only way a tab goes.
	//
	// A request is due the frame after it was asked. The menu asks partway through a frame,
	// after some owners have already drawn, so deferring every request by one frame is what
	// lets all of them see it the same way.
	class TabCloseRequests
	{
	public:
		void Request(std::string name);

		// True once for a name asked before the last EndFrame, false from then on.
		bool Take(std::string_view name);

		// Called once per frame after every owner has drawn. Drops what nobody took - that
		// window closed some other way - and makes this frame's requests due.
		void EndFrame();

	private:
		std::set<std::string, std::less<>> m_Asked;
		std::set<std::string, std::less<>> m_Due;
	};
}
