#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>
#include <git2/sys/errors.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Core {

	namespace {

		// What reading one reference came to: a tag, nothing (the reference is left out), or a failure.
		struct TagRead
		{
			std::optional<Tag> Listed;
			std::optional<Error> Failure;
		};

		TagRead Skipped()
		{
			return TagRead{};
		}

		TagRead Failed(Error failure)
		{
			return TagRead{.Listed = std::nullopt, .Failure = std::move(failure)};
		}

		TagRead ListedAs(Tag tag)
		{
			return TagRead{.Listed = std::move(tag), .Failure = std::nullopt};
		}

		std::chrono::sys_seconds ToSysSeconds(git_time_t time)
		{
			return std::chrono::sys_seconds{std::chrono::seconds{time}};
		}

		std::string FirstLineOf(const char* message)
		{
			const std::string_view text = message == nullptr ? std::string_view{} : std::string_view{message};
			std::string_view line = text.substr(0, text.find('\n'));

			if (line.ends_with('\r'))
			{
				line.remove_suffix(1);
			}

			return std::string{line};
		}

		// A missing object leaves the reference out; anything else fails the listing.
		TagRead ReadFailure(int result, std::string_view action)
		{
			if (IsMissingObject(result))
			{
				return Skipped();
			}

			return Failed(ObjectReadFailure(result, action));
		}

		TagRead ReadLightweightTag(Tag tag, git_repository* handle, const git_oid& commitId)
		{
			CommitHandle commit;
			git_error_clear();

			if (const int lookupResult = git_commit_lookup(Out(commit), handle, &commitId); lookupResult != 0)
			{
				return ReadFailure(lookupResult, "cannot read a tag's commit");
			}

			tag.Target = ToOid(commitId);
			tag.When = ToSysSeconds(git_commit_time(commit.get()));
			return ListedAs(std::move(tag));
		}

		// Follows the tag objects to what they name. Skipped when that is not a commit, or is missing.
		TagRead ReadAnnotatedTag(Tag tag, git_repository* handle, const git_oid& tagObjectId)
		{
			TagHandle outerTag;
			git_error_clear();

			if (const int lookupResult = git_tag_lookup(Out(outerTag), handle, &tagObjectId); lookupResult != 0)
			{
				return ReadFailure(lookupResult, "cannot read a tag");
			}

			tag.Annotated = true;
			tag.Subject = FirstLineOf(git_tag_message(outerTag.get()));

			ObjectHandle target;
			git_error_clear();

			if (const int targetResult = git_tag_target(Out(target), outerTag.get()); targetResult != 0)
			{
				return ReadFailure(targetResult, "cannot read what a tag names");
			}

			while (git_object_type(target.get()) == GIT_OBJECT_TAG)
			{
				ObjectHandle next;
				git_error_clear();

				// The object is a tag, so the cast only names what it already is.
				if (const int nextResult = git_tag_target(Out(next), reinterpret_cast<git_tag*>(target.get())); nextResult != 0)
				{
					return ReadFailure(nextResult, "cannot read what a tag names");
				}

				target = std::move(next);
			}

			if (git_object_type(target.get()) != GIT_OBJECT_COMMIT)
			{
				return Skipped();
			}

			tag.Target = ToOid(*git_object_id(target.get()));

			if (const git_signature* tagger = git_tag_tagger(outerTag.get()); tagger != nullptr)
			{
				tag.When = ToSysSeconds(tagger->when.time);
			}
			else
			{
				// The object is a commit, so the cast only names what it already is.
				tag.When = ToSysSeconds(git_commit_time(reinterpret_cast<git_commit*>(target.get())));
			}

			return ListedAs(std::move(tag));
		}

		TagRead ReadTag(git_repository* handle, const git_reference* reference, TagCache& cache)
		{
			const char* fullName = git_reference_name(reference);
			const git_oid* targetId = git_reference_target(reference);

			// A symbolic ref under refs/tags names another ref rather than an object.
			if (fullName == nullptr || targetId == nullptr)
			{
				return Skipped();
			}

			Tag tag;
			tag.FullName = fullName;
			tag.Name = tag.FullName.substr(kTagPrefix.size());

			if (const std::optional<TagCacheEntry> known = cache.Find(ToOid(*targetId)); known.has_value())
			{
				tag.Annotated = true;
				tag.Target = known->Target;
				tag.When = known->When;
				tag.Subject = known->Subject;
				return ListedAs(std::move(tag));
			}

			ObjectHandle object;
			git_error_clear();

			if (const int lookupResult = git_object_lookup(Out(object), handle, targetId, GIT_OBJECT_ANY); lookupResult != 0)
			{
				return ReadFailure(lookupResult, "cannot read a tag's object");
			}

			const git_object_t type = git_object_type(object.get());

			if (type == GIT_OBJECT_COMMIT)
			{
				return ReadLightweightTag(std::move(tag), handle, *targetId);
			}

			if (type != GIT_OBJECT_TAG)
			{
				return Skipped();
			}

			TagRead read = ReadAnnotatedTag(std::move(tag), handle, *targetId);

			if (read.Listed.has_value())
			{
				cache.Store(ToOid(*targetId), TagCacheEntry{.Target = read.Listed->Target, .When = read.Listed->When,
															.Subject = read.Listed->Subject});
			}

			return read;
		}

		bool ComesBeforeInTagList(const Tag& tag, const Tag& otherTag)
		{
			if (tag.When != otherTag.When)
			{
				return tag.When > otherTag.When;
			}

			return tag.Name < otherTag.Name;
		}
	}

	Result<std::vector<Tag>> Repository::QueryTags(TagCache& cache) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		ReferenceIteratorHandle iterator;
		const std::string pattern = std::string{kTagPrefix} + "*";

		if (git_reference_iterator_glob_new(Out(iterator), m_Handle.get(), pattern.c_str()) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot list the tags: {}", LastLibgit2Message())};
		}

		std::vector<Tag> tags;
		int nextResult = 0;

		while (true)
		{
			ReferenceHandle reference;
			nextResult = git_reference_next(Out(reference), iterator.get());

			if (nextResult != 0)
			{
				break;
			}

			TagRead read = ReadTag(m_Handle.get(), reference.get(), cache);

			if (read.Failure.has_value())
			{
				return Error{read.Failure->Code, std::format("cannot list the tags: {}", read.Failure->Message)};
			}

			if (read.Listed.has_value())
			{
				tags.push_back(std::move(*read.Listed));
			}
		}

		// Anything but GIT_ITEROVER means the listing stopped partway, and a partial list must not pass for the
		// whole one.
		if (nextResult != GIT_ITEROVER)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot list the tags: {}", LastLibgit2Message())};
		}

		std::sort(tags.begin(), tags.end(), ComesBeforeInTagList);

		return tags;
	}
}
