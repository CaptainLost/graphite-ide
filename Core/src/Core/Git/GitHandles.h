#pragma once

// Owning handles for the libgit2 objects the Repository queries acquire. Private to
// Core/Git: include it from a .cpp only, never from a header that leaves the module.
//
// A handle frees its object on every path out of the scope that holds it, so an early
// return - or a throw from std::format or an allocation - between acquiring an object and
// the end of the query cannot leak it.
//
// The rule that no header under Core/Git includes <git2.h> applies here too: the types are
// forward-declared, and the one function object that frees them is defined in
// GitHandles.cpp, which does include it.

#include <memory>

struct git_blob;
struct git_branch_iterator;
struct git_commit;
struct git_diff;
struct git_index;
struct git_object;
struct git_reference;
struct git_revwalk;
struct git_signature;
struct git_status_list;
struct git_tree;
struct git_tree_entry;
struct git_worktree;

namespace Core {

	// One overload per type, each calling that type's git_xxx_free.
	struct Libgit2Free
	{
		void operator()(git_blob* blob) const;
		void operator()(git_branch_iterator* iterator) const;
		void operator()(git_commit* commit) const;
		void operator()(git_diff* diff) const;
		void operator()(git_index* index) const;
		void operator()(git_object* object) const;
		void operator()(git_reference* reference) const;
		void operator()(git_revwalk* walk) const;
		void operator()(git_signature* signature) const;
		void operator()(git_status_list* list) const;
		void operator()(git_tree* tree) const;
		void operator()(git_tree_entry* entry) const;
		void operator()(git_worktree* worktree) const;
	};

	using BlobHandle = std::unique_ptr<git_blob, Libgit2Free>;
	using BranchIteratorHandle = std::unique_ptr<git_branch_iterator, Libgit2Free>;
	using CommitHandle = std::unique_ptr<git_commit, Libgit2Free>;
	using DiffHandle = std::unique_ptr<git_diff, Libgit2Free>;
	using IndexHandle = std::unique_ptr<git_index, Libgit2Free>;
	using ObjectHandle = std::unique_ptr<git_object, Libgit2Free>;
	using ReferenceHandle = std::unique_ptr<git_reference, Libgit2Free>;
	using RevwalkHandle = std::unique_ptr<git_revwalk, Libgit2Free>;
	using SignatureHandle = std::unique_ptr<git_signature, Libgit2Free>;
	using StatusListHandle = std::unique_ptr<git_status_list, Libgit2Free>;
	using TreeHandle = std::unique_ptr<git_tree, Libgit2Free>;
	using TreeEntryHandle = std::unique_ptr<git_tree_entry, Libgit2Free>;
	using WorktreeHandle = std::unique_ptr<git_worktree, Libgit2Free>;

	// Lets a handle stand where libgit2 wants an out-parameter: Out(commit) converts to the
	// git_commit** the call writes into, and the handle takes what was written once the
	// call's full expression ends - before the next statement, and before the body of an
	// `if` whose condition made the call. The object the handle held before, if any, is
	// freed at that moment. So the handle must not be read in the same expression as the
	// call - `f(Out(h)) == 0 && h != nullptr` still sees the old value - only after it.
	template<typename Handle>
	class OutHandle
	{
	public:
		explicit OutHandle(Handle& handle) : m_Handle(handle) {}

		OutHandle(const OutHandle&) = delete;
		OutHandle& operator=(const OutHandle&) = delete;

		~OutHandle() { m_Handle.reset(m_Raw); }

		operator typename Handle::pointer*() { return &m_Raw; }

	private:
		Handle& m_Handle;
		typename Handle::pointer m_Raw = nullptr;
	};

	template<typename Handle>
	OutHandle<Handle> Out(Handle& handle)
	{
		return OutHandle<Handle>(handle);
	}
}
