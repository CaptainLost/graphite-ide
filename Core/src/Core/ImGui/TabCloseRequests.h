#pragma once

#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace Core {

	class TabCloseRequests
	{
	public:
		void Request(std::string_view name);

		bool Take(std::string_view name);

		void EndFrame();

	private:
		std::set<std::string, std::less<>> m_AskedWindowIdentities;
		std::set<std::string, std::less<>> m_DueWindowIdentities;
	};
}
