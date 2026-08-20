#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Base/Log.h"

#include <git2.h>

#include <format>
#include <string>
#include <string_view>

namespace Core {

	namespace {

		std::string LastMessage()
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

		// Returns the offset immediately after the line terminator that starts at `pos` —
		// "\r\n" or a bare "\n" — or npos if `pos` is not the start of one. Git does not
		// normalise line endings inside a commit message, so a message written with CRLF
		// keeps it.
		std::size_t SkipTerminator(std::string_view text, std::size_t pos)
		{
			if (pos < text.size() && text[pos] == '\r' && pos + 1 < text.size() &&
				text[pos + 1] == '\n')
			{
				return pos + 2;
			}

			if (pos < text.size() && text[pos] == '\n')
			{
				return pos + 1;
			}

			return std::string_view::npos;
		}

		Signature ToSignature(const git_signature* who)
		{
			if (who == nullptr)
			{
				return Signature{};
			}

			return Signature{who->name != nullptr ? who->name : "",
							 who->email != nullptr ? who->email : "",
							 std::chrono::sys_seconds{std::chrono::seconds{who->when.time}}};
		}

		CommitSummary ToSummary(git_commit* commit)
		{
			CommitSummary summary;
			summary.Id = ToOid(*git_commit_id(commit));

			const unsigned int parents = git_commit_parentcount(commit);
			summary.Parents.reserve(parents);

			for (unsigned int index = 0; index < parents; ++index)
			{
				summary.Parents.push_back(ToOid(*git_commit_parent_id(commit, index)));
			}

			summary.Author = ToSignature(git_commit_author(commit));
			summary.Committer = ToSignature(git_commit_committer(commit));

			if (const char* line = git_commit_summary(commit); line != nullptr)
			{
				summary.Summary = line;
			}

			// git_commit_body() trims the surrounding whitespace, which loses the trailing
			// newline a message was written with. The body is read out of the raw message
			// instead, so a caller that writes the commit back out (or diffs it against
			// what was typed) sees exactly what git recorded.
			if (const char* raw = git_commit_message(commit); raw != nullptr)
			{
				const std::string_view text(raw);
				const std::size_t titleEnd = text.find_first_of("\r\n");

				if (titleEnd != std::string_view::npos)
				{
					std::size_t bodyStart = SkipTerminator(text, titleEnd);

					if (bodyStart != std::string_view::npos)
					{
						// Exactly one blank line separates the title from the body, per
						// git convention — only that single separator is consumed here.
						// A second blank line is the body's own leading content and must
						// survive, matching what `git log --format=%b` shows.
						if (const std::size_t afterBlank = SkipTerminator(text, bodyStart);
							afterBlank != std::string_view::npos)
						{
							bodyStart = afterBlank;
						}

						if (bodyStart < text.size())
						{
							summary.Body = text.substr(bodyStart);
						}
					}
				}
			}

			return summary;
		}
	}

	Result<std::vector<CommitSummary>> Repository::QueryLog(const LogRequest& request) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_revwalk* walk = nullptr;

		if (git_revwalk_new(&walk, m_Handle) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot walk the history: {}", LastMessage())};
		}

		// Topological first so a branch stays contiguous, date second so two unrelated
		// branches interleave the way a reader expects.
		git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);

		const int pushed = request.Ref.has_value()
							   ? git_revwalk_push_ref(walk, request.Ref->c_str())
							   : git_revwalk_push_head(walk);

		if (pushed != 0)
		{
			// An unborn HEAD lands here, and so does a ref that was deleted between the
			// panel listing it and this call. Neither is worth a failure the user has to
			// read; both mean "there is nothing to show".
			const std::string message = LastMessage();
			git_revwalk_free(walk);

			if (request.Ref.has_value())
			{
				return Error{ErrorCode::NotFound,
							 std::format("no such ref '{}': {}", *request.Ref, message)};
			}

			return std::vector<CommitSummary>{};
		}

		std::vector<CommitSummary> commits;
		git_oid id{};

		while (commits.size() < request.MaxCount && git_revwalk_next(&id, walk) == 0)
		{
			git_commit* commit = nullptr;

			if (git_commit_lookup(&commit, m_Handle, &id) != 0)
			{
				CORE_TRACE("Skipping an unreadable commit while walking the history");
				continue;
			}

			commits.push_back(ToSummary(commit));
			git_commit_free(commit);
		}

		git_revwalk_free(walk);
		return commits;
	}
}
