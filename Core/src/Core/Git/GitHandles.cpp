#include "corepch.h"

#include "Core/Git/GitHandles.h"

#include <git2.h>

namespace Core {

	void Libgit2Free::operator()(git_blob* blob) const noexcept
	{
		git_blob_free(blob);
	}

	void Libgit2Free::operator()(git_branch_iterator* iterator) const noexcept
	{
		git_branch_iterator_free(iterator);
	}

	void Libgit2Free::operator()(git_commit* commit) const noexcept
	{
		git_commit_free(commit);
	}

	void Libgit2Free::operator()(git_diff* diff) const noexcept
	{
		git_diff_free(diff);
	}

	void Libgit2Free::operator()(git_index* index) const noexcept
	{
		git_index_free(index);
	}

	void Libgit2Free::operator()(git_object* object) const noexcept
	{
		git_object_free(object);
	}

	void Libgit2Free::operator()(git_reference* reference) const noexcept
	{
		git_reference_free(reference);
	}

	void Libgit2Free::operator()(git_revwalk* walk) const noexcept
	{
		git_revwalk_free(walk);
	}

	void Libgit2Free::operator()(git_signature* signature) const noexcept
	{
		git_signature_free(signature);
	}

	void Libgit2Free::operator()(git_status_list* list) const noexcept
	{
		git_status_list_free(list);
	}

	void Libgit2Free::operator()(git_tree* tree) const noexcept
	{
		git_tree_free(tree);
	}

	void Libgit2Free::operator()(git_tree_entry* entry) const noexcept
	{
		git_tree_entry_free(entry);
	}
}
