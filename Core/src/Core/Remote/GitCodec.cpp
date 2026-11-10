#include "corepch.h"

#include "Core/Remote/GitCodec.h"

#include "Core/FileSystem/FileSystem.h"

#include <array>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

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
}
