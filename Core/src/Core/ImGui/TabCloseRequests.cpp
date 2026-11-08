#include "corepch.h"

#include "Core/ImGui/TabCloseRequests.h"

namespace Core {

	namespace {
		std::string_view WindowIdentityOf(std::string_view name)
		{
			const std::size_t identityMarkerPosition = name.rfind("###");

			return identityMarkerPosition == std::string_view::npos ? name : name.substr(identityMarkerPosition);
		}
	}

	void TabCloseRequests::Request(std::string_view name, CloseAsked how)
	{
		const auto [asked, added] = m_AskedWindowIdentities.emplace(WindowIdentityOf(name), how);

		// Asked by name once is asked by name, whatever else asked for it too.
		if (!added && how == CloseAsked::ByName)
		{
			asked->second = CloseAsked::ByName;
		}
	}

	bool TabCloseRequests::Take(std::string_view name)
	{
		const auto dueRequest = m_DueWindowIdentities.find(WindowIdentityOf(name));

		if (dueRequest == m_DueWindowIdentities.end())
		{
			return false;
		}

		m_DueWindowIdentities.erase(dueRequest);
		return true;
	}

	bool TabCloseRequests::TakeAskedByName(std::string_view name)
	{
		const auto dueRequest = m_DueWindowIdentities.find(WindowIdentityOf(name));

		if (dueRequest == m_DueWindowIdentities.end() || dueRequest->second != CloseAsked::ByName)
		{
			return false;
		}

		m_DueWindowIdentities.erase(dueRequest);
		return true;
	}

	void TabCloseRequests::EndFrame()
	{
		m_DueWindowIdentities = std::exchange(m_AskedWindowIdentities, {});
	}
}
