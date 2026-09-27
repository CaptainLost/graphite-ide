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
	//
	// Names are matched by window identity, the part ImGui identifies a window by: from
	// "###" on when the name has one, the whole name otherwise. The visible part of a title
	// can change between the frame a request is asked and the frame it is taken - a session
	// renamed, a document given a suffix to tell it from another - and it is still the same
	// window.
	class TabCloseRequests
	{
	public:
		void Request(std::string_view name);

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
