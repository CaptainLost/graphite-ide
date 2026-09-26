#include "corepch.h"

#include "Core/Git/Libgit2Support.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"

#include <git2.h>

namespace Core {

	std::string LastLibgit2Message()
	{
		const git_error* detail = git_error_last();

		return detail != nullptr && detail->message != nullptr ? detail->message
															   : "unknown error";
	}

	Oid ToOid(const git_oid& id)
	{
		Oid result;

		for (std::size_t index = 0; index < result.Bytes.size(); ++index)
		{
			result.Bytes[index] = static_cast<std::uint8_t>(id.id[index]);
		}

		return result;
	}

	git_oid ToGitOid(const Oid& id)
	{
		git_oid result{};

		for (std::size_t index = 0; index < id.Bytes.size(); ++index)
		{
			result.id[index] = id.Bytes[index];
		}

		return result;
	}

	std::string ToGitPath(const std::filesystem::path& relative)
	{
		std::string text = ToUtf8(relative);

		for (char& character : text)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}

		return text;
	}

	void EnsureLibgit2Initialised()
	{
		static const int result = git_libgit2_init();

		if (result < 0)
			CORE_ERROR("libgit2 failed to initialise (code {})", result);
	}
}
