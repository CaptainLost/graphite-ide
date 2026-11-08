#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace Core {

	// How a tab came to be asked to close: its own Close, or one of several that Close Others, Close to the Right or
	// Close All picked.
	enum class CloseAsked
	{
		ByName,
		AmongOthers
	};

	class TabCloseRequests
	{
	public:
		void Request(std::string_view name) { Request(name, CloseAsked::ByName); }
		void Request(std::string_view name, CloseAsked how);

		// True once for a tab asked to close in either way.
		bool Take(std::string_view name);

		// True once for a tab asked to close by its own Close only. A panel takes its requests this way, so closing the
		// documents beside it in bulk leaves it open.
		bool TakeAskedByName(std::string_view name);

		void EndFrame();

	private:
		std::map<std::string, CloseAsked, std::less<>> m_AskedWindowIdentities;
		std::map<std::string, CloseAsked, std::less<>> m_DueWindowIdentities;
	};
}
