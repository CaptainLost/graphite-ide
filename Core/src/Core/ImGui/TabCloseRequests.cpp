#include "corepch.h"

#include "Core/ImGui/TabCloseRequests.h"

namespace Core {

	namespace {
		// The part of a window name ImGui identifies the window by: from the last "###" on,
		// marker included, or the whole name when there is none. ImGui's string hash starts
		// over at every "###", so two names that agree from there on are one window.
		std::string_view Identity(std::string_view name)
		{
			const std::size_t marker = name.rfind("###");

			return marker == std::string_view::npos ? name : name.substr(marker);
		}
	}

	void TabCloseRequests::Request(std::string_view name)
	{
		m_Asked.emplace(Identity(name));
	}

	bool TabCloseRequests::Take(std::string_view name)
	{
		const auto found = m_Due.find(Identity(name));

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
