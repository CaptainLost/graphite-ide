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

		struct DecodedEntry
		{
			MarkKey Key;
			ViewedMark Mark;
		};

		std::optional<DecodedEntry> DecodeEntry(const nlohmann::json& entry)
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

			return DecodedEntry{.Key = MarkKey{.Group = *group, .Path = Core::FromUtf8(*path)},
								.Mark = ViewedMark{.Old = *oldId, .New = *newId}};
		}

		ViewedMarks::BranchMarks DecodeBranchMarks(const nlohmann::json& entries)
		{
			ViewedMarks::BranchMarks branchMarks;

			for (const nlohmann::json& entry : entries)
			{
				const std::optional<DecodedEntry> decoded = DecodeEntry(entry);

				if (decoded.has_value())
				{
					branchMarks.insert_or_assign(decoded->Key, decoded->Mark);
				}
			}

			return branchMarks;
		}
	}

	ViewedState ViewedMarks::StateOf(const std::string& branch, ReviewGroup group, const Core::FileDiff& file) const
	{
		if (!m_Marks.contains(branch))
		{
			return ViewedState::NotViewed;
		}

		const BranchMarks& branchMarks = m_Marks.at(branch);
		const MarkKey key{.Group = group, .Path = ShownPath(file)};

		if (!branchMarks.contains(key))
		{
			return ViewedState::NotViewed;
		}

		const ViewedMark& mark = branchMarks.at(key);
		const bool sameChange = mark.Old == file.OldId && mark.New == file.NewId;
		return sameChange ? ViewedState::Viewed : ViewedState::ChangedSinceViewed;
	}

	void ViewedMarks::Mark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file)
	{
		const MarkKey key{.Group = group, .Path = ShownPath(file)};
		m_Marks[branch].insert_or_assign(key, ViewedMark{.Old = file.OldId, .New = file.NewId});

		++m_Version;
	}

	void ViewedMarks::Unmark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file)
	{
		if (!m_Marks.contains(branch))
		{
			return;
		}

		BranchMarks& branchMarks = m_Marks.at(branch);
		const std::size_t removed = branchMarks.erase(MarkKey{.Group = group, .Path = ShownPath(file)});

		if (branchMarks.empty())
		{
			m_Marks.erase(branch);
		}

		if (removed > 0)
		{
			++m_Version;
		}
	}

	void ViewedMarks::Prune(const std::vector<Core::Branch>& branches)
	{
		std::set<std::string> missingNow;
		std::vector<std::string> missingTwice;

		for (const auto& [fullName, branchMarks] : m_Marks)
		{
			if (ListsBranch(branches, fullName))
			{
				continue;
			}

			if (m_MissingLastTime.contains(fullName))
			{
				missingTwice.push_back(fullName);
				continue;
			}

			missingNow.insert(fullName);
		}

		for (const std::string& fullName : missingTwice)
		{
			m_Marks.erase(fullName);
		}

		m_MissingLastTime = std::move(missingNow);

		if (!missingTwice.empty())
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

		BranchMarks& target = m_Marks[to];

		for (const auto& [key, mark] : moved.mapped())
		{
			target.insert_or_assign(key, mark);
		}

		++m_Version;
	}

	void ViewedMarks::Replace(Marks marks)
	{
		m_Marks = std::move(marks);
		m_MissingLastTime.clear();
		++m_Version;
	}

	std::string EncodeViewedMarks(const ViewedMarks::Marks& marks)
	{
		nlohmann::json branches = nlohmann::json::object();

		for (const auto& [fullName, branchMarks] : marks)
		{
			nlohmann::json entries = nlohmann::json::array();

			for (const auto& [key, mark] : branchMarks)
			{
				entries.push_back(nlohmann::json{{kGroupKey, NameOfGroup(key.Group)},
												 {kPathKey, Core::ToUtf8(key.Path)},
												 {kOldKey, mark.Old.ToHex()},
												 {kNewKey, mark.New.ToHex()}});
			}

			branches[fullName] = std::move(entries);
		}

		const nlohmann::json document{{kVersionKey, kFormatVersion}, {kBranchesKey, std::move(branches)}};

		return document.dump(kIndentWidth, kIndentCharacter, kKeepNonAscii, nlohmann::json::error_handler_t::replace);
	}

	DecodedMarks DecodeViewedMarksText(std::string_view json)
	{
		const DecodedMarks malformed{.Text = MarksText::Malformed};

		const nlohmann::json::parser_callback_t noCallback = nullptr;
		const bool allowExceptions = false;
		const nlohmann::json document = nlohmann::json::parse(json, noCallback, allowExceptions);

		if (document.is_discarded() || !document.is_object())
		{
			return malformed;
		}

		const nlohmann::json::const_iterator version = document.find(kVersionKey);

		if (version == document.end() || !version->is_number_integer())
		{
			return malformed;
		}

		if (version->get<int>() != kFormatVersion)
		{
			return DecodedMarks{.Text = MarksText::OtherVersion};
		}

		const nlohmann::json::const_iterator branches = document.find(kBranchesKey);

		if (branches == document.end() || !branches->is_object())
		{
			return malformed;
		}

		DecodedMarks decoded{.Text = MarksText::Read};

		for (const auto& [fullName, entries] : branches->items())
		{
			if (!entries.is_array())
			{
				continue;
			}

			ViewedMarks::BranchMarks branchMarks = DecodeBranchMarks(entries);

			if (!branchMarks.empty())
			{
				decoded.Marks[fullName] = std::move(branchMarks);
			}
		}

		return decoded;
	}

	ViewedMarks::Marks DecodeViewedMarks(std::string_view json)
	{
		return DecodeViewedMarksText(json).Marks;
	}
}
