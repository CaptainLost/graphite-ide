#include "corepch.h"

#include "Core/ImGui/TabCloseRequests.h"

namespace Core {

	void TabCloseRequests::Request(std::string name)
	{
		m_Asked.insert(std::move(name));
	}

	bool TabCloseRequests::Take(std::string_view name)
	{
		const auto found = m_Due.find(name);

		if (found == m_Due.end())
			return false;

		m_Due.erase(found);
		return true;
	}

	void TabCloseRequests::EndFrame()
	{
		m_Due = std::exchange(m_Asked, {});
	}
}
