#include "corepch.h"

#include "Core/Git/GitHandles.h"

#include <git2.h>

namespace Core {

	void Libgit2Free::operator()(git_blob* blob) const
	{
		git_blob_free(blob);
	}

	void Libgit2Free::operator()(git_branch_iterator* iterator) const
	{
		git_branch_iterator_free(iterator);
	}

	void Libgit2Free::operator()(git_commit* commit) const
	{
		git_commit_free(commit);
	}

	void Libgit2Free::operator()(git_diff* diff) const
	{
		git_diff_free(diff);
	}

	void Libgit2Free::operator()(git_index* index) const
	{
		git_index_free(index);
	}

	void Libgit2Free::operator()(git_object* object) const
	{
		git_object_free(object);
	}

	void Libgit2Free::operator()(git_reference* reference) const
	{
		git_reference_free(reference);
	}

	void Libgit2Free::operator()(git_revwalk* walk) const
	{
		git_revwalk_free(walk);
	}

	void Libgit2Free::operator()(git_signature* signature) const
	{
		git_signature_free(signature);
	}

	void Libgit2Free::operator()(git_status_list* list) const
	{
		git_status_list_free(list);
	}

	void Libgit2Free::operator()(git_tree* tree) const
	{
		git_tree_free(tree);
	}

	void Libgit2Free::operator()(git_tree_entry* entry) const
	{
		git_tree_entry_free(entry);
	}

	void Libgit2Free::operator()(git_worktree* worktree) const
	{
		git_worktree_free(worktree);
	}
}
