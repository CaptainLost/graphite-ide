#include "corepch.h"

#include "Core/Git/Libgit2Support.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"

#include <git2.h>

#include <format>

namespace Core {

	namespace {

		constexpr const char* kLocalBranchPrefix = "refs/heads/";

		Error IndexReadFailure()
		{
			return Error{.Code = ErrorCode::IoFailure,
						 .Message = std::format("cannot read the index: {}", LastLibgit2Message())};
		}
	}

	std::string LastLibgit2Message()
	{
		const git_error* lastError = git_error_last();
		const bool hasMessage = lastError != nullptr && lastError->message != nullptr;

		return hasMessage ? lastError->message : "unknown error";
	}

	Result<IndexHandle> OpenFreshIndex(git_repository* handle)
	{
		IndexHandle index;

		if (git_repository_index(Out(index), handle) != 0)
		{
			return IndexReadFailure();
		}

		const bool reloadEvenIfChecksumUnchanged = false;

		if (git_index_read(index.get(), reloadEvenIfChecksumUnchanged) != 0)
		{
			return IndexReadFailure();
		}

		return index;
	}

	Result<void> RefreshIndex(git_repository* handle)
	{
		Result<IndexHandle> openResult = OpenFreshIndex(handle);

		if (!openResult.HasValue())
		{
			return openResult.GetError();
		}

		return Result<void>::Ok();
	}

	std::string TextOrEmpty(const char* text)
	{
		return text != nullptr ? text : "";
	}

	std::string LocalBranchReference(const std::string& branchName)
	{
		return kLocalBranchPrefix + branchName;
	}

	Oid ToOid(const git_oid& id)
	{
		Oid oid;

		for (std::size_t byteIndex = 0; byteIndex < oid.Bytes.size(); ++byteIndex)
		{
			oid.Bytes[byteIndex] = static_cast<std::uint8_t>(id.id[byteIndex]);
		}

		return oid;
	}

	git_oid ToGitOid(const Oid& id)
	{
		git_oid gitOid{};

		for (std::size_t byteIndex = 0; byteIndex < id.Bytes.size(); ++byteIndex)
		{
			gitOid.id[byteIndex] = id.Bytes[byteIndex];
		}

		return gitOid;
	}

	std::string ToGitPath(const std::filesystem::path& relative)
	{
		std::string gitPath = ToUtf8(relative);

		for (char& character : gitPath)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}

		return gitPath;
	}

	PathSpec::PathSpec(const std::vector<std::filesystem::path>& paths)
	{
		m_GitPaths.reserve(paths.size());
		m_GitPathPointers.reserve(paths.size());

		for (const std::filesystem::path& path : paths)
		{
			m_GitPaths.push_back(ToGitPath(path));
		}

		for (const std::string& gitPath : m_GitPaths)
		{
			m_GitPathPointers.push_back(gitPath.c_str());
		}
	}

	git_strarray PathSpec::Get() const
	{
		// The one cast libgit2's signature forces: git_strarray holds char**, though every call
		// that takes a pathspec only reads it.
		return git_strarray{.strings = const_cast<char**>(m_GitPathPointers.data()), .count = m_GitPathPointers.size()};
	}

	void EnsureLibgit2Initialised()
	{
		static const int initialiseCode = git_libgit2_init();

		if (initialiseCode < 0)
		{
			CORE_ERROR("libgit2 failed to initialise (code {})", initialiseCode);
		}
	}
}
