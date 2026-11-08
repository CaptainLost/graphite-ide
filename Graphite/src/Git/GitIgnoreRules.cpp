#include "Git/GitIgnoreRules.h"

#include <utility>

namespace Graphite {

	GitIgnoreRules::GitIgnoreRules(const std::filesystem::path& root)
	{
		if (Core::Result<Core::Repository> opened = Core::Repository::Open(root))
		{
			m_Repository.emplace(std::move(opened.GetValue()));
		}
	}

	bool GitIgnoreRules::IsIgnored(const std::filesystem::path& path) const
	{
		return m_Repository && m_Repository->IsIgnored(path);
	}
}
