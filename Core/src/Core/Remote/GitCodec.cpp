#include "corepch.h"

#include "Core/Remote/GitCodec.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Remote/AgentProtocol.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace Core {

	namespace {

		// Thrown inside a decoder and turned into InvalidArgument at its edge, so each field read
		// stays one line.
		struct MissingField
		{
			std::string Name;
		};

		template<typename Value>
		Value Field(const nlohmann::json& object, const char* name)
		{
			const auto found = object.find(name);

			if (found == object.end())
			{
				throw MissingField{name};
			}

			try
			{
				return found->get<Value>();
			}
			catch (const nlohmann::json::exception&)
			{
				throw MissingField{name};
			}
		}

		template<typename Value>
		std::optional<Value> OptionalField(const nlohmann::json& object, const char* name)
		{
			const auto found = object.find(name);

			if (found == object.end() || found->is_null())
			{
				return std::nullopt;
			}

			return Field<Value>(object, name);
		}

		const nlohmann::json& ObjectAt(const nlohmann::json& json, const char* what)
		{
			if (!json.is_object())
			{
				throw MissingField{what};
			}

			return json;
		}

		const nlohmann::json& ArrayField(const nlohmann::json& object, const char* name)
		{
			const auto found = object.find(name);

			if (found == object.end() || !found->is_array())
			{
				throw MissingField{name};
			}

			return *found;
		}

		std::string PathText(const std::filesystem::path& path)
		{
			return ToGenericUtf8(path);
		}

		std::filesystem::path PathFrom(const nlohmann::json& object, const char* name)
		{
			return FromUtf8(Field<std::string>(object, name));
		}

		std::optional<std::filesystem::path> OptionalPathFrom(const nlohmann::json& object, const char* name)
		{
			const std::optional<std::string> text = OptionalField<std::string>(object, name);
			return text ? std::optional{FromUtf8(*text)} : std::nullopt;
		}

		std::string OidText(const Oid& id)
		{
			return id.ToHex();
		}

		Oid OidFrom(const nlohmann::json& object, const char* name)
		{
			const std::optional<Oid> id = Oid::FromHex(Field<std::string>(object, name));

			if (!id)
			{
				throw MissingField{name};
			}

			return *id;
		}

		std::optional<Oid> OptionalOidFrom(const nlohmann::json& object, const char* name)
		{
			return object.contains(name) && !object.at(name).is_null() ? std::optional{OidFrom(object, name)} : std::nullopt;
		}

		// An enum by name, so a reordering on either side cannot shift meanings.
		template<typename Enum, std::size_t Count>
		using EnumNames = std::array<std::pair<Enum, std::string_view>, Count>;

		template<typename Enum, std::size_t Count>
		std::string NameOf(const EnumNames<Enum, Count>& names, Enum value)
		{
			for (const auto& [candidate, name] : names)
			{
				if (candidate == value)
				{
					return std::string{name};
				}
			}

			return std::string{names.front().second};
		}

		template<typename Enum, std::size_t Count>
		Enum EnumFrom(const EnumNames<Enum, Count>& names, const nlohmann::json& object, const char* field)
		{
			const std::string text = Field<std::string>(object, field);

			for (const auto& [candidate, name] : names)
			{
				if (name == text)
				{
					return candidate;
				}
			}

			throw MissingField{field};
		}

		constexpr EnumNames<HeadKind, 3> kHeadKinds{{{HeadKind::Branch, "branch"}, {HeadKind::Detached, "detached"}, {HeadKind::Unborn, "unborn"}}};

		constexpr EnumNames<RepositoryOperation, 7> kOperations{{{RepositoryOperation::None, "none"},
																  {RepositoryOperation::Merge, "merge"},
																  {RepositoryOperation::Rebase, "rebase"},
																  {RepositoryOperation::CherryPick, "cherryPick"},
																  {RepositoryOperation::Revert, "revert"},
																  {RepositoryOperation::Bisect, "bisect"},
																  {RepositoryOperation::ApplyMailbox, "applyMailbox"}}};

		constexpr EnumNames<DiffTarget, 6> kDiffTargets{{{DiffTarget::WorkTreeToIndex, "workTreeToIndex"},
														  {DiffTarget::IndexToHead, "indexToHead"},
														  {DiffTarget::WorkTreeToHead, "workTreeToHead"},
														  {DiffTarget::CommitToParent, "commitToParent"},
														  {DiffTarget::StashToParent, "stashToParent"},
														  {DiffTarget::RevisionToRevision, "revisionToRevision"}}};

		constexpr EnumNames<WhitespaceInDiff, 2> kWhitespace{{{WhitespaceInDiff::Shown, "shown"}, {WhitespaceInDiff::Ignored, "ignored"}}};

		constexpr EnumNames<FileChange, 7> kChanges{{{FileChange::Unmodified, "unmodified"},
													 {FileChange::Added, "added"},
													 {FileChange::Modified, "modified"},
													 {FileChange::Deleted, "deleted"},
													 {FileChange::Renamed, "renamed"},
													 {FileChange::Copied, "copied"},
													 {FileChange::TypeChanged, "typeChanged"}}};

		constexpr EnumNames<DiffLineKind, 3> kLineKinds{{{DiffLineKind::Context, "context"}, {DiffLineKind::Added, "added"}, {DiffLineKind::Removed, "removed"}}};

		FileStatus FileStatusFrom(const nlohmann::json& json)
		{
			const nlohmann::json& object = ObjectAt(json, "entry");

			return FileStatus{
				.Path = PathFrom(object, "path"),
				.RenamedFrom = OptionalPathFrom(object, "renamedFrom"),
				.Index = static_cast<StatusFlags>(Field<std::uint8_t>(object, "index")),
				.WorkTree = static_cast<StatusFlags>(Field<std::uint8_t>(object, "workTree")),
				.IsConflicted = Field<bool>(object, "conflicted"),
				.IsUntracked = Field<bool>(object, "untracked"),
				.IsDirectory = Field<bool>(object, "directory"),
				.HoldsCheckout = Field<bool>(object, "holdsCheckout"),
			};
		}

		DiffLine DiffLineFrom(const nlohmann::json& json)
		{
			const nlohmann::json& object = ObjectAt(json, "line");

			return DiffLine{
				.Kind = EnumFrom(kLineKinds, object, "kind"),
				.OldLine = Field<int>(object, "old"),
				.NewLine = Field<int>(object, "new"),
				.Text = Field<std::string>(object, "text"),
			};
		}

		Hunk HunkFrom(const nlohmann::json& json)
		{
			const nlohmann::json& object = ObjectAt(json, "hunk");
			Hunk hunk{
				.OldStart = Field<int>(object, "oldStart"),
				.OldCount = Field<int>(object, "oldCount"),
				.NewStart = Field<int>(object, "newStart"),
				.NewCount = Field<int>(object, "newCount"),
				.Header = Field<std::string>(object, "header"),
			};

			for (const nlohmann::json& line : ArrayField(object, "lines"))
			{
				hunk.Lines.push_back(DiffLineFrom(line));
			}

			return hunk;
		}

		FileDiff FileDiffFrom(const nlohmann::json& json)
		{
			const nlohmann::json& object = ObjectAt(json, "file");
			FileDiff file{
				.OldPath = PathFrom(object, "oldPath"),
				.NewPath = PathFrom(object, "newPath"),
				.Change = EnumFrom(kChanges, object, "change"),
				.OldId = OidFrom(object, "oldId"),
				.NewId = OidFrom(object, "newId"),
				.IsBinary = Field<bool>(object, "binary"),
			};

			for (const nlohmann::json& hunk : ArrayField(object, "hunks"))
			{
				file.Hunks.push_back(HunkFrom(hunk));
			}

			return file;
		}

		constexpr EnumNames<RefKind, 2> kRefKinds{{{RefKind::Local, "local"}, {RefKind::Remote, "remote"}}};

		constexpr EnumNames<CheckoutKind, 3> kCheckoutKinds{
			{{CheckoutKind::LocalBranch, "localBranch"}, {CheckoutKind::RemoteBranch, "remoteBranch"}, {CheckoutKind::Commit, "commit"}}};

		std::int64_t SecondsOf(std::chrono::sys_seconds when)
		{
			return when.time_since_epoch().count();
		}

		std::chrono::sys_seconds SecondsFrom(const nlohmann::json& object, const char* name)
		{
			return std::chrono::sys_seconds{std::chrono::seconds{Field<std::int64_t>(object, name)}};
		}

		nlohmann::json OidList(const std::vector<Oid>& ids)
		{
			nlohmann::json listed = nlohmann::json::array();

			for (const Oid& id : ids)
			{
				listed.push_back(OidText(id));
			}

			return listed;
		}

		std::vector<Oid> OidListFrom(const nlohmann::json& object, const char* name)
		{
			std::vector<Oid> ids;

			for (const nlohmann::json& text : ArrayField(object, name))
			{
				const std::optional<Oid> id = text.is_string() ? Oid::FromHex(text.get<std::string>()) : std::nullopt;

				if (!id)
				{
					throw MissingField{name};
				}

				ids.push_back(*id);
			}

			return ids;
		}

		nlohmann::json PathList(const std::vector<std::filesystem::path>& paths)
		{
			nlohmann::json listed = nlohmann::json::array();

			for (const std::filesystem::path& path : paths)
			{
				listed.push_back(PathText(path));
			}

			return listed;
		}

		std::vector<std::filesystem::path> PathListFrom(const nlohmann::json& object, const char* name)
		{
			std::vector<std::filesystem::path> paths;

			for (const nlohmann::json& text : ArrayField(object, name))
			{
				if (!text.is_string())
				{
					throw MissingField{name};
				}

				paths.push_back(FromUtf8(text.get<std::string>()));
			}

			return paths;
		}

		nlohmann::json SignatureJson(const Signature& signature)
		{
			return nlohmann::json{{"name", signature.Name}, {"email", signature.Email}, {"when", SecondsOf(signature.When)}};
		}

		Signature SignatureFrom(const nlohmann::json& json, const char* what)
		{
			const nlohmann::json& object = ObjectAt(json, what);
			return Signature{.Name = Field<std::string>(object, "name"), .Email = Field<std::string>(object, "email"), .When = SecondsFrom(object, "when")};
		}

		CommitSummary CommitSummaryFrom(const nlohmann::json& json)
		{
			const nlohmann::json& object = ObjectAt(json, "commit");

			return CommitSummary{
				.Id = OidFrom(object, "id"),
				.Parents = OidListFrom(object, "parents"),
				.Author = SignatureFrom(object.contains("author") ? object.at("author") : nlohmann::json{}, "author"),
				.Committer = SignatureFrom(object.contains("committer") ? object.at("committer") : nlohmann::json{}, "committer"),
				.Summary = Field<std::string>(object, "summary"),
				.Body = Field<std::string>(object, "body"),
			};
		}

		Branch BranchFrom(const nlohmann::json& json)
		{
			const nlohmann::json& object = ObjectAt(json, "branch");
			const auto tipCommit = object.find("tipCommit");

			return Branch{
				.Name = Field<std::string>(object, "name"),
				.FullName = Field<std::string>(object, "fullName"),
				.Kind = EnumFrom(kRefKinds, object, "kind"),
				.Tip = OidFrom(object, "tip"),
				.IsHead = Field<bool>(object, "head"),
				.IsRemoteDefault = Field<bool>(object, "remoteDefault"),
				.Upstream = OptionalField<std::string>(object, "upstream"),
				.MissingUpstream = OptionalField<std::string>(object, "missingUpstream"),
				.Ahead = Field<std::size_t>(object, "ahead"),
				.Behind = Field<std::size_t>(object, "behind"),
				.DistanceKnown = Field<bool>(object, "distanceKnown"),
				.TipCommit = tipCommit == object.end() || tipCommit->is_null() ? std::nullopt : std::optional{CommitSummaryFrom(*tipCommit)},
			};
		}

		Tag TagFrom(const nlohmann::json& json)
		{
			const nlohmann::json& object = ObjectAt(json, "tag");

			return Tag{
				.Name = Field<std::string>(object, "name"),
				.FullName = Field<std::string>(object, "fullName"),
				.Target = OidFrom(object, "target"),
				.When = SecondsFrom(object, "when"),
				.Annotated = Field<bool>(object, "annotated"),
				.Subject = Field<std::string>(object, "subject"),
			};
		}

		Remote RemoteFrom(const nlohmann::json& json)
		{
			const nlohmann::json& object = ObjectAt(json, "remote");
			return Remote{.Name = Field<std::string>(object, "name"), .Url = Field<std::string>(object, "url")};
		}

		TipAuthors TipAuthorsFrom(const nlohmann::json& json)
		{
			const nlohmann::json& object = ObjectAt(json, "tip authors");
			TipAuthors authors{.Tip = OidFrom(object, "tip"), .OwnCommits = Field<std::size_t>(object, "ownCommits"), .Truncated = Field<bool>(object, "truncated")};

			for (const nlohmann::json& author : ArrayField(object, "authors"))
			{
				authors.Authors.push_back(SignatureFrom(author, "authors"));
			}

			return authors;
		}

		StashEntry StashFrom(const nlohmann::json& json)
		{
			const nlohmann::json& object = ObjectAt(json, "stash");

			return StashEntry{
				.Index = Field<std::size_t>(object, "index"),
				.Message = Field<std::string>(object, "message"),
				.Id = OidFrom(object, "id"),
				.SecondsSinceEpoch = Field<std::int64_t>(object, "seconds"),
			};
		}

		template<typename Value, typename DecodeOne>
		std::vector<Value> ListFrom(const nlohmann::json& json, const char* what, DecodeOne decodeOne)
		{
			if (!json.is_array())
			{
				throw MissingField{what};
			}

			std::vector<Value> values;
			values.reserve(json.size());

			for (const nlohmann::json& one : json)
			{
				values.push_back(decodeOne(one));
			}

			return values;
		}

		// Runs a decoder, turning a missing or mistyped field into InvalidArgument naming it.
		template<typename Value, typename Decode>
		Result<Value> Decoded(std::string_view what, Decode decode)
		{
			try
			{
				return decode();
			}
			catch (const MissingField& missing)
			{
				return Error{ErrorCode::InvalidArgument, std::format("{} has no valid \"{}\"", what, missing.Name)};
			}
		}
	}

	nlohmann::json ToJson(const FileStatus& status)
	{
		nlohmann::json json{
			{"path", PathText(status.Path)},
			{"index", static_cast<std::uint8_t>(status.Index)},
			{"workTree", static_cast<std::uint8_t>(status.WorkTree)},
			{"conflicted", status.IsConflicted},
			{"untracked", status.IsUntracked},
			{"directory", status.IsDirectory},
			{"holdsCheckout", status.HoldsCheckout},
		};

		if (status.RenamedFrom)
		{
			json["renamedFrom"] = PathText(*status.RenamedFrom);
		}

		return json;
	}

	nlohmann::json ToJson(const WorkingTreeStatus& status)
	{
		nlohmann::json entries = nlohmann::json::array();

		for (const FileStatus& entry : status.Entries)
		{
			entries.push_back(ToJson(entry));
		}

		return nlohmann::json{{"entries", std::move(entries)}};
	}

	nlohmann::json ToJson(const HeadState& head)
	{
		nlohmann::json json{
			{"kind", NameOf(kHeadKinds, head.Kind)},
			{"branch", head.BranchName},
			{"operation", NameOf(kOperations, head.Operation)},
		};

		if (head.Commit)
		{
			json["commit"] = OidText(*head.Commit);
		}

		return json;
	}

	nlohmann::json ToJson(const DiffRequest& request)
	{
		nlohmann::json json{
			{"target", NameOf(kDiffTargets, request.Target)},
			{"whitespace", NameOf(kWhitespace, request.Whitespace)},
		};

		if (request.From)
		{
			json["from"] = OidText(*request.From);
		}

		if (request.To)
		{
			json["to"] = OidText(*request.To);
		}

		if (request.File)
		{
			json["file"] = PathText(*request.File);
		}

		return json;
	}

	nlohmann::json ToJson(const Diff& diff)
	{
		nlohmann::json files = nlohmann::json::array();

		for (const FileDiff& file : diff.Files)
		{
			nlohmann::json hunks = nlohmann::json::array();

			for (const Hunk& hunk : file.Hunks)
			{
				nlohmann::json lines = nlohmann::json::array();

				for (const DiffLine& line : hunk.Lines)
				{
					lines.push_back({{"kind", NameOf(kLineKinds, line.Kind)}, {"old", line.OldLine}, {"new", line.NewLine}, {"text", line.Text}});
				}

				hunks.push_back({{"oldStart", hunk.OldStart},
								 {"oldCount", hunk.OldCount},
								 {"newStart", hunk.NewStart},
								 {"newCount", hunk.NewCount},
								 {"header", hunk.Header},
								 {"lines", std::move(lines)}});
			}

			files.push_back({{"oldPath", PathText(file.OldPath)},
							 {"newPath", PathText(file.NewPath)},
							 {"change", NameOf(kChanges, file.Change)},
							 {"oldId", OidText(file.OldId)},
							 {"newId", OidText(file.NewId)},
							 {"binary", file.IsBinary},
							 {"hunks", std::move(hunks)}});
		}

		return nlohmann::json{{"files", std::move(files)}};
	}

	nlohmann::json ToJson(const CommitRequest& request)
	{
		return nlohmann::json{{"message", request.Message}, {"amend", request.Amend}};
	}

	Result<WorkingTreeStatus> WorkingTreeStatusFromJson(const nlohmann::json& json)
	{
		return Decoded<WorkingTreeStatus>("a status", [&] {
			WorkingTreeStatus status;

			for (const nlohmann::json& entry : ArrayField(ObjectAt(json, "status"), "entries"))
			{
				status.Entries.push_back(FileStatusFrom(entry));
			}

			return status;
		});
	}

	Result<HeadState> HeadStateFromJson(const nlohmann::json& json)
	{
		return Decoded<HeadState>("HEAD", [&] {
			const nlohmann::json& object = ObjectAt(json, "head");

			return HeadState{
				.Kind = EnumFrom(kHeadKinds, object, "kind"),
				.BranchName = Field<std::string>(object, "branch"),
				.Commit = OptionalOidFrom(object, "commit"),
				.Operation = EnumFrom(kOperations, object, "operation"),
			};
		});
	}

	Result<DiffRequest> DiffRequestFromJson(const nlohmann::json& json)
	{
		return Decoded<DiffRequest>("a diff request", [&] {
			const nlohmann::json& object = ObjectAt(json, "request");

			return DiffRequest{
				.Target = EnumFrom(kDiffTargets, object, "target"),
				.From = OptionalOidFrom(object, "from"),
				.To = OptionalOidFrom(object, "to"),
				.File = OptionalPathFrom(object, "file"),
				.Whitespace = EnumFrom(kWhitespace, object, "whitespace"),
			};
		});
	}

	Result<Diff> DiffFromJson(const nlohmann::json& json)
	{
		return Decoded<Diff>("a diff", [&] {
			Diff diff;

			for (const nlohmann::json& file : ArrayField(ObjectAt(json, "diff"), "files"))
			{
				diff.Files.push_back(FileDiffFrom(file));
			}

			return diff;
		});
	}

	Result<CommitRequest> CommitRequestFromJson(const nlohmann::json& json)
	{
		return Decoded<CommitRequest>("a commit request", [&] {
			const nlohmann::json& object = ObjectAt(json, "request");
			return CommitRequest{.Message = Field<std::string>(object, "message"), .Amend = Field<bool>(object, "amend")};
		});
	}

	nlohmann::json ToJson(const CommitSummary& commit)
	{
		return nlohmann::json{
			{"id", OidText(commit.Id)},
			{"parents", OidList(commit.Parents)},
			{"author", SignatureJson(commit.Author)},
			{"committer", SignatureJson(commit.Committer)},
			{"summary", commit.Summary},
			{"body", commit.Body},
		};
	}

	nlohmann::json ToJson(const Branch& branch)
	{
		nlohmann::json json{
			{"name", branch.Name},
			{"fullName", branch.FullName},
			{"kind", NameOf(kRefKinds, branch.Kind)},
			{"tip", OidText(branch.Tip)},
			{"head", branch.IsHead},
			{"remoteDefault", branch.IsRemoteDefault},
			{"ahead", branch.Ahead},
			{"behind", branch.Behind},
			{"distanceKnown", branch.DistanceKnown},
		};

		if (branch.Upstream)
		{
			json["upstream"] = *branch.Upstream;
		}

		if (branch.MissingUpstream)
		{
			json["missingUpstream"] = *branch.MissingUpstream;
		}

		if (branch.TipCommit)
		{
			json["tipCommit"] = ToJson(*branch.TipCommit);
		}

		return json;
	}

	nlohmann::json ToJson(const Tag& tag)
	{
		return nlohmann::json{
			{"name", tag.Name},
			{"fullName", tag.FullName},
			{"target", OidText(tag.Target)},
			{"when", SecondsOf(tag.When)},
			{"annotated", tag.Annotated},
			{"subject", tag.Subject},
		};
	}

	nlohmann::json ToJson(const Remote& remote)
	{
		return nlohmann::json{{"name", remote.Name}, {"url", remote.Url}};
	}

	nlohmann::json ToJson(const LogRequest& request)
	{
		nlohmann::json json{
			{"alsoFrom", OidList(request.AlsoFrom)},
			{"firstParentOnly", request.FirstParentOnly},
			{"filter", request.Filter.Text},
			{"followRenames", request.FollowRenames},
			{"maxWalk", request.MaxWalk},
			{"maxCount", request.MaxCount},
		};

		if (request.Ref)
		{
			json["ref"] = *request.Ref;
		}

		for (const auto& [name, id] : {std::pair{"from", &request.From}, std::pair{"exclude", &request.Exclude}, std::pair{"stash", &request.Stash}})
		{
			if (*id)
			{
				json[name] = OidText(**id);
			}
		}

		if (request.File)
		{
			json["file"] = PathText(*request.File);
		}

		if (request.Lines)
		{
			json["lines"] = {{"first", request.Lines->First}, {"last", request.Lines->Last}};
		}

		return json;
	}

	nlohmann::json ToJson(const LogPage& page)
	{
		nlohmann::json commits = nlohmann::json::array();

		for (const CommitSummary& commit : page.Commits)
		{
			commits.push_back(ToJson(commit));
		}

		return nlohmann::json{
			{"commits", std::move(commits)},
			{"complete", page.Complete},
			{"walked", page.Walked},
			{"unpushed", OidList(std::vector<Oid>{page.Unpushed.begin(), page.Unpushed.end()})},
			{"upstreamKnown", page.UpstreamKnown},
		};
	}

	nlohmann::json ToJson(const OwnAuthorsRequest& request)
	{
		return nlohmann::json{{"tips", OidList(request.Tips)}, {"hidden", OidList(request.Hidden)}, {"maxCommitsPerTip", request.MaxCommitsPerTip}};
	}

	nlohmann::json ToJson(const TipAuthors& authors)
	{
		nlohmann::json signatures = nlohmann::json::array();

		for (const Signature& author : authors.Authors)
		{
			signatures.push_back(SignatureJson(author));
		}

		return nlohmann::json{
			{"tip", OidText(authors.Tip)},
			{"authors", std::move(signatures)},
			{"ownCommits", authors.OwnCommits},
			{"truncated", authors.Truncated},
		};
	}

	nlohmann::json ToJson(const StashEntry& stash)
	{
		return nlohmann::json{{"index", stash.Index}, {"message", stash.Message}, {"id", OidText(stash.Id)}, {"seconds", stash.SecondsSinceEpoch}};
	}

	nlohmann::json ToJson(const CheckoutTarget& target)
	{
		return nlohmann::json{{"kind", NameOf(kCheckoutKinds, target.Kind)}, {"name", target.Name}, {"commit", OidText(target.Commit)}};
	}

	nlohmann::json ToJson(const CheckoutPlan& plan)
	{
		return nlohmann::json{{"overwritten", PathList(plan.Overwritten)}, {"updated", PathList(plan.Updated)}};
	}

	nlohmann::json ToJson(const CheckoutOutcome& outcome)
	{
		nlohmann::json json{{"changed", PathList(outcome.Changed)}};

		if (outcome.Failure)
		{
			json["failure"] = {{"code", std::string{ErrorCodeName(outcome.Failure->Code)}}, {"message", outcome.Failure->Message}};
		}

		return json;
	}

	Result<std::vector<Branch>> BranchesFromJson(const nlohmann::json& json)
	{
		return Decoded<std::vector<Branch>>("a branch list", [&] { return ListFrom<Branch>(json, "branches", BranchFrom); });
	}

	Result<std::vector<Tag>> TagsFromJson(const nlohmann::json& json)
	{
		return Decoded<std::vector<Tag>>("a tag list", [&] { return ListFrom<Tag>(json, "tags", TagFrom); });
	}

	Result<std::vector<Remote>> RemotesFromJson(const nlohmann::json& json)
	{
		return Decoded<std::vector<Remote>>("a remote list", [&] { return ListFrom<Remote>(json, "remotes", RemoteFrom); });
	}

	Result<LogRequest> LogRequestFromJson(const nlohmann::json& json)
	{
		return Decoded<LogRequest>("a log request", [&] {
			const nlohmann::json& object = ObjectAt(json, "request");
			LogRequest request{
				.Ref = OptionalField<std::string>(object, "ref"),
				.From = OptionalOidFrom(object, "from"),
				.AlsoFrom = OidListFrom(object, "alsoFrom"),
				.FirstParentOnly = Field<bool>(object, "firstParentOnly"),
				.Exclude = OptionalOidFrom(object, "exclude"),
				.Stash = OptionalOidFrom(object, "stash"),
				.File = OptionalPathFrom(object, "file"),
				.Filter = {.Text = Field<std::string>(object, "filter")},
				.FollowRenames = Field<bool>(object, "followRenames"),
				.MaxWalk = Field<std::size_t>(object, "maxWalk"),
				.MaxCount = Field<std::size_t>(object, "maxCount"),
			};

			if (const auto lines = object.find("lines"); lines != object.end() && !lines->is_null())
			{
				const nlohmann::json& range = ObjectAt(*lines, "lines");
				request.Lines = OneBasedLineRange{.First = Field<std::size_t>(range, "first"), .Last = Field<std::size_t>(range, "last")};
			}

			return request;
		});
	}

	Result<LogPage> LogPageFromJson(const nlohmann::json& json)
	{
		return Decoded<LogPage>("a log page", [&] {
			const nlohmann::json& object = ObjectAt(json, "page");
			LogPage page;

			for (const nlohmann::json& commit : ArrayField(object, "commits"))
			{
				page.Commits.push_back(CommitSummaryFrom(commit));
			}

			page.Complete = Field<bool>(object, "complete");
			page.Walked = Field<std::size_t>(object, "walked");

			for (const Oid& id : OidListFrom(object, "unpushed"))
			{
				page.Unpushed.insert(id);
			}

			page.UpstreamKnown = Field<bool>(object, "upstreamKnown");
			return page;
		});
	}

	Result<OwnAuthorsRequest> OwnAuthorsRequestFromJson(const nlohmann::json& json)
	{
		return Decoded<OwnAuthorsRequest>("an own-authors request", [&] {
			const nlohmann::json& object = ObjectAt(json, "request");

			return OwnAuthorsRequest{
				.Tips = OidListFrom(object, "tips"),
				.Hidden = OidListFrom(object, "hidden"),
				.MaxCommitsPerTip = Field<std::size_t>(object, "maxCommitsPerTip"),
			};
		});
	}

	Result<std::vector<TipAuthors>> TipAuthorsFromJson(const nlohmann::json& json)
	{
		return Decoded<std::vector<TipAuthors>>("a tip authors list", [&] { return ListFrom<TipAuthors>(json, "tip authors", TipAuthorsFrom); });
	}

	Result<std::vector<StashEntry>> StashesFromJson(const nlohmann::json& json)
	{
		return Decoded<std::vector<StashEntry>>("a stash list", [&] { return ListFrom<StashEntry>(json, "stashes", StashFrom); });
	}

	Result<CheckoutTarget> CheckoutTargetFromJson(const nlohmann::json& json)
	{
		return Decoded<CheckoutTarget>("a checkout target", [&] {
			const nlohmann::json& object = ObjectAt(json, "target");
			return CheckoutTarget{.Kind = EnumFrom(kCheckoutKinds, object, "kind"), .Name = Field<std::string>(object, "name"), .Commit = OidFrom(object, "commit")};
		});
	}

	Result<CheckoutPlan> CheckoutPlanFromJson(const nlohmann::json& json)
	{
		return Decoded<CheckoutPlan>("a checkout plan", [&] {
			const nlohmann::json& object = ObjectAt(json, "plan");
			return CheckoutPlan{.Overwritten = PathListFrom(object, "overwritten"), .Updated = PathListFrom(object, "updated")};
		});
	}

	Result<CheckoutOutcome> CheckoutOutcomeFromJson(const nlohmann::json& json)
	{
		return Decoded<CheckoutOutcome>("a checkout outcome", [&] {
			const nlohmann::json& object = ObjectAt(json, "outcome");
			CheckoutOutcome outcome{.Changed = PathListFrom(object, "changed")};

			if (const auto failure = object.find("failure"); failure != object.end() && !failure->is_null())
			{
				const nlohmann::json& described = ObjectAt(*failure, "failure");
				const std::optional<ErrorCode> code = ErrorCodeFromName(Field<std::string>(described, "code"));

				if (!code)
				{
					throw MissingField{"failure"};
				}

				outcome.Failure = Error{*code, Field<std::string>(described, "message")};
			}

			return outcome;
		});
	}
}
