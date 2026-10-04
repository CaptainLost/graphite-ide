#include "corepch.h"

#include "Core/Git/Libgit2Support.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"

#include <git2.h>

#include <format>

namespace Core {

	std::string LastLibgit2Message()
	{
		const git_error* detail = git_error_last();

		return detail != nullptr && detail->message != nullptr ? detail->message
															   : "unknown error";
	}

	Result<IndexHandle> OpenFreshIndex(git_repository* handle)
	{
		IndexHandle index;

		// Two statements: Out hands the index over only at the end of the full expression.
		if (git_repository_index(Out(index), handle) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot read the index: {}", LastLibgit2Message())};
		}

		// Not forced: libgit2 compares the file's checksum and reloads only when it changed.
		if (git_index_read(index.get(), false) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot read the index: {}", LastLibgit2Message())};
		}

		return index;
	}

	Result<void> RefreshIndex(git_repository* handle)
	{
		auto index = OpenFreshIndex(handle);

		if (!index.HasValue())
		{
			return index.GetError();
		}

		return Result<void>::Ok();
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

	PathSpec::PathSpec(const std::vector<std::filesystem::path>& paths)
	{
		m_Text.reserve(paths.size());
		m_Pointers.reserve(paths.size());

		for (const std::filesystem::path& path : paths)
		{
			m_Text.push_back(ToGitPath(path));
		}

		for (const std::string& text : m_Text)
		{
			m_Pointers.push_back(text.c_str());
		}
	}

	git_strarray PathSpec::Get() const
	{
		// The one cast libgit2's signature forces: git_strarray holds char**, though every call
		// that takes a pathspec only reads it.
		return git_strarray{const_cast<char**>(m_Pointers.data()), m_Pointers.size()};
	}

	void EnsureLibgit2Initialised()
	{
		static const int result = git_libgit2_init();

		if (result < 0)
		{
			CORE_ERROR("libgit2 failed to initialise (code {})", result);
		}
	}
}
