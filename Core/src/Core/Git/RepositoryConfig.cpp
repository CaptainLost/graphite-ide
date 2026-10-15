#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Base/Ascii.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <format>
#include <optional>
#include <string>

namespace Core {

	Result<std::optional<std::string>> Repository::ReadUserEmail() const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		// WARNING: git_config_get_string answers only from a snapshot: the text it hands back lives
		// in the snapshot, which a live configuration could change under it.
		ConfigHandle snapshot;

		if (git_repository_config_snapshot(Out(snapshot), m_Handle.get()) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot read the git configuration: {}", LastLibgit2Message())};
		}

		const char* email = nullptr;
		const int lookupResult = git_config_get_string(&email, snapshot.get(), "user.email");

		if (lookupResult == GIT_ENOTFOUND)
		{
			return std::optional<std::string>{};
		}

		if (lookupResult != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot read user.email: {}", LastLibgit2Message())};
		}

		const std::string trimmedEmail = Trimmed(TextOrEmpty(email));

		if (trimmedEmail.empty())
		{
			return std::optional<std::string>{};
		}

		return std::optional<std::string>{trimmedEmail};
	}
}
