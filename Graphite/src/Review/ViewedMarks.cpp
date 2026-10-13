#include "Review/ViewedMarks.h"

#include "Git/History/CommitLabels.h"

#include "Core/Base/ByteText.h"
#include "Core/FileSystem/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		constexpr int kFormatVersion = 1;
		constexpr int kIndentWidth = 2;
		constexpr char kIndentCharacter = ' ';
		constexpr bool kKeepNonAscii = false;

		constexpr const char* kVersionKey = "version";
		constexpr const char* kBranchesKey = "branches";
		constexpr const char* kGroupKey = "group";
		constexpr const char* kPathKey = "path";
		constexpr const char* kOldKey = "old";
		constexpr const char* kNewKey = "new";

		constexpr const char* kCommitsGroupName = "commits";
		constexpr const char* kUncommittedGroupName = "uncommitted";

		bool IsMarkOf(const ViewedMark& mark, ReviewGroup group, const std::filesystem::path& path)
		{
			return mark.Group == group && mark.Path == path;
		}

		const ViewedMark* FindMark(const std::vector<ViewedMark>& marks, ReviewGroup group,
								   const std::filesystem::path& path)
		{
			for (const ViewedMark& mark : marks)
			{
				if (IsMarkOf(mark, group, path))
				{
					return &mark;
				}
			}

			return nullptr;
		}

		bool ListsBranch(const std::vector<Core::Branch>& branches, const std::string& fullName)
		{
			for (const Core::Branch& branch : branches)
			{
				if (branch.FullName == fullName)
				{
					return true;
				}
			}

			return false;
		}

		bool IsUnlistedBranch(const std::vector<Core::Branch>& branches, const ViewedMarks::Marks::value_type& entry)
		{
			const auto& [fullName, branchMarks] = entry;
			return !ListsBranch(branches, fullName);
		}

		std::string NameOfGroup(ReviewGroup group)
		{
			switch (group)
			{
			case ReviewGroup::Commits:
				return kCommitsGroupName;
			case ReviewGroup::Uncommitted:
				return kUncommittedGroupName;
			}

			return kCommitsGroupName;
		}

		std::optional<ReviewGroup> GroupNamed(const std::string& name)
		{
			if (name == kCommitsGroupName)
			{
				return ReviewGroup::Commits;
			}

			if (name == kUncommittedGroupName)
			{
				return ReviewGroup::Uncommitted;
			}

			return std::nullopt;
		}

		std::optional<Core::Oid> OidFromHex(const std::string& hex)
		{
			const std::optional<std::vector<std::uint8_t>> bytes = Core::BytesFromHex(hex);
			Core::Oid id;

			if (!bytes.has_value() || bytes->size() != id.Bytes.size())
			{
				return std::nullopt;
			}

			std::copy(bytes->begin(), bytes->end(), id.Bytes.begin());
			return id;
		}

		std::optional<std::string> StringAt(const nlohmann::json& object, const char* key)
		{
			const nlohmann::json::const_iterator value = object.find(key);

			if (value == object.end() || !value->is_string())
			{
				return std::nullopt;
			}

			return value->get<std::string>();
		}

		std::optional<ViewedMark> DecodeMark(const nlohmann::json& entry)
		{
			if (!entry.is_object())
			{
				return std::nullopt;
			}

			const std::optional<std::string> groupName = StringAt(entry, kGroupKey);
			const std::optional<std::string> path = StringAt(entry, kPathKey);
			const std::optional<std::string> oldHex = StringAt(entry, kOldKey);
			const std::optional<std::string> newHex = StringAt(entry, kNewKey);

			if (!groupName || !path || path->empty() || !oldHex || !newHex)
			{
				return std::nullopt;
			}

			const std::optional<ReviewGroup> group = GroupNamed(*groupName);
			const std::optional<Core::Oid> oldId = OidFromHex(*oldHex);
			const std::optional<Core::Oid> newId = OidFromHex(*newHex);

			if (!group || !oldId || !newId)
			{
				return std::nullopt;
			}

			return ViewedMark{.Group = *group, .Path = Core::FromUtf8(*path), .Old = *oldId, .New = *newId};
		}
	}

	ViewedState ViewedMarks::StateOf(const std::string& branch, ReviewGroup group, const Core::FileDiff& file) const
	{
		const Marks::const_iterator branchMarks = m_Marks.find(branch);

		if (branchMarks == m_Marks.end())
		{
			return ViewedState::NotViewed;
		}

		const ViewedMark* mark = FindMark(branchMarks->second, group, ShownPath(file));

		if (mark == nullptr)
		{
			return ViewedState::NotViewed;
		}

		const bool sameChange = mark->Old == file.OldId && mark->New == file.NewId;
		return sameChange ? ViewedState::Viewed : ViewedState::ChangedSinceViewed;
	}

	void ViewedMarks::Mark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file)
	{
		const std::filesystem::path& path = ShownPath(file);
		std::vector<ViewedMark>& branchMarks = m_Marks[branch];

		std::erase_if(branchMarks, [group, &path](const ViewedMark& mark) { return IsMarkOf(mark, group, path); });
		branchMarks.push_back(ViewedMark{.Group = group, .Path = path, .Old = file.OldId, .New = file.NewId});

		++m_Version;
	}

	void ViewedMarks::Unmark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file)
	{
		const Marks::iterator branchMarks = m_Marks.find(branch);

		if (branchMarks == m_Marks.end())
		{
			return;
		}

		const std::filesystem::path& path = ShownPath(file);
		const std::size_t removed = std::erase_if(
			branchMarks->second, [group, &path](const ViewedMark& mark) { return IsMarkOf(mark, group, path); });

		if (branchMarks->second.empty())
		{
			m_Marks.erase(branchMarks);
		}

		if (removed > 0)
		{
			++m_Version;
		}
	}

	void ViewedMarks::Prune(const std::vector<Core::Branch>& branches)
	{
		const std::size_t removed = std::erase_if(
			m_Marks, [&branches](const Marks::value_type& entry) { return IsUnlistedBranch(branches, entry); });

		if (removed > 0)
		{
			++m_Version;
		}
	}

	void ViewedMarks::Rename(const std::string& from, const std::string& to)
	{
		Marks::node_type moved = m_Marks.extract(from);

		if (moved.empty())
		{
			return;
		}

		std::vector<ViewedMark>& target = m_Marks[to];
		target.insert(target.end(), moved.mapped().begin(), moved.mapped().end());

		++m_Version;
	}

	void ViewedMarks::Replace(Marks marks)
	{
		m_Marks = std::move(marks);
		++m_Version;
	}

	std::string EncodeViewedMarks(const ViewedMarks::Marks& marks)
	{
		nlohmann::json branches = nlohmann::json::object();

		for (const auto& [fullName, branchMarks] : marks)
		{
			nlohmann::json entries = nlohmann::json::array();

			for (const ViewedMark& mark : branchMarks)
			{
				entries.push_back(nlohmann::json{{kGroupKey, NameOfGroup(mark.Group)},
												 {kPathKey, Core::ToUtf8(mark.Path)},
												 {kOldKey, mark.Old.ToHex()},
												 {kNewKey, mark.New.ToHex()}});
			}

			branches[fullName] = std::move(entries);
		}

		const nlohmann::json document{{kVersionKey, kFormatVersion}, {kBranchesKey, std::move(branches)}};

		return document.dump(kIndentWidth, kIndentCharacter, kKeepNonAscii, nlohmann::json::error_handler_t::replace);
	}

	ViewedMarks::Marks DecodeViewedMarks(std::string_view json)
	{
		ViewedMarks::Marks marks;

		const nlohmann::json::parser_callback_t noCallback = nullptr;
		const bool allowExceptions = false;
		const nlohmann::json document = nlohmann::json::parse(json, noCallback, allowExceptions);

		if (document.is_discarded() || !document.is_object())
		{
			return marks;
		}

		const nlohmann::json::const_iterator version = document.find(kVersionKey);

		if (version == document.end() || !version->is_number_integer() || version->get<int>() != kFormatVersion)
		{
			return marks;
		}

		const nlohmann::json::const_iterator branches = document.find(kBranchesKey);

		if (branches == document.end() || !branches->is_object())
		{
			return marks;
		}

		for (const auto& [fullName, entries] : branches->items())
		{
			if (!entries.is_array())
			{
				continue;
			}

			std::vector<ViewedMark> branchMarks;

			for (const nlohmann::json& entry : entries)
			{
				std::optional<ViewedMark> mark = DecodeMark(entry);

				if (mark.has_value())
				{
					branchMarks.push_back(std::move(*mark));
				}
			}

			if (!branchMarks.empty())
			{
				marks[fullName] = std::move(branchMarks);
			}
		}

		return marks;
	}
}
