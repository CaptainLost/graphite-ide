#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Fs/FileSystem.h"

#include <git2.h>

#include <format>
#include <string>

namespace Core {

	namespace {

		std::string LastMessage()
		{
			const git_error* detail = git_error_last();

			return detail != nullptr && detail->message != nullptr ? detail->message
																   : "unknown error";
		}

		// git speaks forward slashes on every platform; a relative path may carry native
		// separators.
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

		FileChange ToChange(git_delta_t status)
		{
			switch (status)
			{
				case GIT_DELTA_ADDED: return FileChange::Added;
				case GIT_DELTA_DELETED: return FileChange::Deleted;
				case GIT_DELTA_MODIFIED: return FileChange::Modified;
				case GIT_DELTA_RENAMED: return FileChange::Renamed;
				case GIT_DELTA_COPIED: return FileChange::Copied;
				case GIT_DELTA_TYPECHANGE: return FileChange::TypeChanged;
				default: return FileChange::Unmodified;
			}
		}

		// Collected by the three callbacks below, which libgit2 calls in file, hunk, line
		// order — so the open file is always the last one appended.
		struct Collector
		{
			Diff Result;
		};

		int OnFile(const git_diff_delta* delta, float, void* payload)
		{
			Collector& collector = *static_cast<Collector*>(payload);

			FileDiff file;
			file.Change = ToChange(delta->status);
			file.IsBinary = (delta->flags & GIT_DIFF_FLAG_BINARY) != 0;

			if (delta->old_file.path != nullptr && delta->status != GIT_DELTA_ADDED)
			{
				file.OldPath = FromUtf8(delta->old_file.path);
			}

			if (delta->new_file.path != nullptr && delta->status != GIT_DELTA_DELETED)
			{
				file.NewPath = FromUtf8(delta->new_file.path);
			}

			collector.Result.Files.push_back(std::move(file));
			return 0;
		}

		int OnHunk(const git_diff_delta*, const git_diff_hunk* hunk, void* payload)
		{
			Collector& collector = *static_cast<Collector*>(payload);

			if (collector.Result.Files.empty())
			{
				return 0;
			}

			Hunk entry;
			entry.OldStart = hunk->old_start;
			entry.OldCount = hunk->old_lines;
			entry.NewStart = hunk->new_start;
			entry.NewCount = hunk->new_lines;
			entry.Header.assign(hunk->header, hunk->header_len);

			collector.Result.Files.back().Hunks.push_back(std::move(entry));
			return 0;
		}

		int OnLine(const git_diff_delta*, const git_diff_hunk*, const git_diff_line* line,
				   void* payload)
		{
			Collector& collector = *static_cast<Collector*>(payload);

			if (collector.Result.Files.empty() || collector.Result.Files.back().Hunks.empty())
			{
				return 0;
			}

			DiffLine entry;

			switch (line->origin)
			{
				case GIT_DIFF_LINE_ADDITION: entry.Kind = DiffLineKind::Added; break;
				case GIT_DIFF_LINE_DELETION: entry.Kind = DiffLineKind::Removed; break;
				case GIT_DIFF_LINE_CONTEXT: entry.Kind = DiffLineKind::Context; break;

				// The end-of-file-newline markers carry no line of the file. Letting them
				// through would draw a phantom line in the view.
				default: return 0;
			}

			entry.OldLine = line->old_lineno > 0 ? line->old_lineno : 0;
			entry.NewLine = line->new_lineno > 0 ? line->new_lineno : 0;
			entry.Text.assign(line->content, line->content_len);

			collector.Result.Files.back().Hunks.back().Lines.push_back(std::move(entry));
			return 0;
		}
	}

	Result<Diff> Repository::QueryDiff(const DiffRequest& request) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_diff_options options = GIT_DIFF_OPTIONS_INIT;

		std::string pathspec;
		const char* pathspecEntry = nullptr;

		if (request.File.has_value())
		{
			pathspec = ToGitPath(*request.File);
			pathspecEntry = pathspec.c_str();
			options.pathspec.strings = const_cast<char**>(&pathspecEntry);
			options.pathspec.count = 1;
		}

		// Looks up a commit's tree, or gives back null for a request that has no such side.
		const auto treeOf = [this](const std::optional<Oid>& id) -> git_tree* {
			if (!id.has_value())
			{
				return nullptr;
			}

			git_oid raw{};
			for (std::size_t index = 0; index < id->Bytes.size(); ++index)
			{
				raw.id[index] = id->Bytes[index];
			}

			git_commit* commit = nullptr;
			if (git_commit_lookup(&commit, m_Handle, &raw) != 0)
			{
				return nullptr;
			}

			git_tree* tree = nullptr;
			if (git_commit_tree(&tree, commit) != 0)
			{
				tree = nullptr;
			}

			git_commit_free(commit);
			return tree;
		};

		const auto headTree = [this]() -> git_tree* {
			git_oid headId{};

			if (git_reference_name_to_id(&headId, m_Handle, "HEAD") != 0)
			{
				return nullptr;
			}

			git_commit* commit = nullptr;
			if (git_commit_lookup(&commit, m_Handle, &headId) != 0)
			{
				return nullptr;
			}

			git_tree* tree = nullptr;
			if (git_commit_tree(&tree, commit) != 0)
			{
				tree = nullptr;
			}

			git_commit_free(commit);
			return tree;
		};

		git_diff* diff = nullptr;
		git_tree* left = nullptr;
		git_tree* right = nullptr;
		int created = -1;

		switch (request.Target)
		{
			case DiffTarget::WorkTreeToIndex:
				created = git_diff_index_to_workdir(&diff, m_Handle, nullptr, &options);
				break;

			case DiffTarget::IndexToHead:
				left = headTree();
				created = git_diff_tree_to_index(&diff, m_Handle, left, nullptr, &options);
				break;

			case DiffTarget::WorkTreeToHead:
				left = headTree();
				created = git_diff_tree_to_workdir_with_index(&diff, m_Handle, left, &options);
				break;

			case DiffTarget::CommitToParent:
			{
				if (!request.From.has_value())
				{
					return Error{ErrorCode::InvalidArgument,
								 "a commit-to-parent diff needs a commit"};
				}

				right = treeOf(request.From);

				git_oid raw{};
				for (std::size_t index = 0; index < request.From->Bytes.size(); ++index)
				{
					raw.id[index] = request.From->Bytes[index];
				}

				git_commit* commit = nullptr;
				if (git_commit_lookup(&commit, m_Handle, &raw) == 0)
				{
					// First parent only. A merge shown against every parent at once reads
					// as a diff nobody wrote; the view offers the other parents separately.
					if (git_commit_parentcount(commit) > 0)
					{
						git_commit* parent = nullptr;

						if (git_commit_parent(&parent, commit, 0) == 0)
						{
							if (git_commit_tree(&left, parent) != 0)
							{
								left = nullptr;
							}

							git_commit_free(parent);
						}
					}

					git_commit_free(commit);
				}

				created = git_diff_tree_to_tree(&diff, m_Handle, left, right, &options);
				break;
			}

			case DiffTarget::RevisionToRevision:
				left = treeOf(request.From);
				right = treeOf(request.To);
				created = git_diff_tree_to_tree(&diff, m_Handle, left, right, &options);
				break;
		}

		if (created != 0 || diff == nullptr)
		{
			const std::string message = LastMessage();

			git_tree_free(right);
			git_tree_free(left);

			return Error{ErrorCode::IoFailure, std::format("cannot diff: {}", message)};
		}

		git_diff_find_options findOptions = GIT_DIFF_FIND_OPTIONS_INIT;
		findOptions.flags = GIT_DIFF_FIND_RENAMES | GIT_DIFF_FIND_COPIES;
		git_diff_find_similar(diff, &findOptions);

		Collector collector;
		const int walked = git_diff_foreach(diff, OnFile, nullptr, OnHunk, OnLine, &collector);

		git_diff_free(diff);
		git_tree_free(right);
		git_tree_free(left);

		if (walked != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the diff: {}", LastMessage())};
		}

		return std::move(collector.Result);
	}
}
