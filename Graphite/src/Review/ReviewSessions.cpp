#include "Review/ReviewSessions.h"

#include "Git/History/CommitLabels.h"

#include "Core/FileSystem/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace Graphite {

	namespace {

		constexpr int kFormatVersion = 2;
		constexpr int kIndentWidth = 2;
		constexpr char kIndentCharacter = ' ';
		constexpr bool kKeepNonAscii = false;

		constexpr const char* kVersionKey = "version";
		constexpr const char* kSessionsKey = "sessions";
		constexpr const char* kBaseKey = "base";
		constexpr const char* kIncludeUncommittedKey = "includeUncommitted";
		constexpr const char* kFlatKey = "flat";
		constexpr const char* kHideViewedKey = "hideViewed";
		constexpr const char* kHideWhitespaceKey = "hideWhitespace";
		constexpr const char* kSinceViewedKey = "sinceViewed";
		constexpr const char* kMarksKey = "marks";
		constexpr const char* kGroupKey = "group";
		constexpr const char* kPathKey = "path";
		constexpr const char* kOldKey = "old";
		constexpr const char* kNewKey = "new";

		constexpr const char* kCommitsGroupName = "commits";
		constexpr const char* kUncommittedGroupName = "uncommitted";

		ReviewSession* SessionNamed(ReviewSessions::Sessions& sessions, const std::string& branch)
		{
			const ReviewSessions::Sessions::iterator found = sessions.find(branch);

			if (found == sessions.end())
			{
				return nullptr;
			}

			auto& [name, session] = *found;
			return &session;
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

		std::optional<std::string> StringAt(const nlohmann::json& object, const char* key)
		{
			const nlohmann::json::const_iterator value = object.find(key);

			if (value == object.end() || !value->is_string())
			{
				return std::nullopt;
			}

			return value->get<std::string>();
		}

		bool BoolAt(const nlohmann::json& object, const char* key, bool fallback)
		{
			const nlohmann::json::const_iterator value = object.find(key);

			if (value == object.end() || !value->is_boolean())
			{
				return fallback;
			}

			return value->get<bool>();
		}

		struct DecodedMark
		{
			MarkKey Key;
			ViewedMark Mark;
		};

		std::optional<DecodedMark> DecodeMark(const nlohmann::json& entry)
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
			const std::optional<Core::Oid> oldId = Core::Oid::FromHex(*oldHex);
			const std::optional<Core::Oid> newId = Core::Oid::FromHex(*newHex);

			if (!group || !oldId || !newId)
			{
				return std::nullopt;
			}

			return DecodedMark{.Key = MarkKey{.Group = *group, .Path = Core::FromUtf8(*path)},
							   .Mark = ViewedMark{.Old = *oldId, .New = *newId}};
		}

		nlohmann::json EncodeMarks(const BranchMarks& marks)
		{
			nlohmann::json entries = nlohmann::json::array();

			for (const auto& [key, mark] : marks)
			{
				entries.push_back(nlohmann::json{{kGroupKey, NameOfGroup(key.Group)},
												 {kPathKey, Core::ToUtf8(key.Path)},
												 {kOldKey, mark.Old.ToHex()},
												 {kNewKey, mark.New.ToHex()}});
			}

			return entries;
		}

		nlohmann::json EncodeSession(const ReviewSession& session)
		{
			const SessionSettings& settings = session.Settings;

			return nlohmann::json{{kBaseKey, session.Base},
								  {kIncludeUncommittedKey, settings.IncludeUncommitted},
								  {kFlatKey, settings.Shape == ListShape::Flat},
								  {kHideViewedKey, settings.HideViewed},
								  {kHideWhitespaceKey, settings.Whitespace == Core::WhitespaceInDiff::Ignored},
								  {kSinceViewedKey, settings.ChangedFiles == ChangedFileView::SinceViewed},
								  {kMarksKey, EncodeMarks(session.Marks)}};
		}

		std::optional<ReviewSession> DecodeSession(const nlohmann::json& entry)
		{
			if (!entry.is_object())
			{
				return std::nullopt;
			}

			const std::optional<std::string> base = StringAt(entry, kBaseKey);

			if (!base || base->empty())
			{
				return std::nullopt;
			}

			const SessionSettings defaults;
			ReviewSession session;
			session.Base = *base;
			session.Settings.IncludeUncommitted = BoolAt(entry, kIncludeUncommittedKey, defaults.IncludeUncommitted);

			const bool flatByDefault = defaults.Shape == ListShape::Flat;
			session.Settings.Shape = BoolAt(entry, kFlatKey, flatByDefault) ? ListShape::Flat : ListShape::Tree;
			session.Settings.HideViewed = BoolAt(entry, kHideViewedKey, defaults.HideViewed);

			const bool ignoredByDefault = defaults.Whitespace == Core::WhitespaceInDiff::Ignored;
			session.Settings.Whitespace = BoolAt(entry, kHideWhitespaceKey, ignoredByDefault)
											  ? Core::WhitespaceInDiff::Ignored
											  : Core::WhitespaceInDiff::Shown;

			const bool sinceViewedByDefault = defaults.ChangedFiles == ChangedFileView::SinceViewed;
			session.Settings.ChangedFiles = BoolAt(entry, kSinceViewedKey, sinceViewedByDefault)
												? ChangedFileView::SinceViewed
												: ChangedFileView::AllChanges;

			const nlohmann::json::const_iterator marks = entry.find(kMarksKey);

			if (marks != entry.end() && marks->is_array())
			{
				for (const nlohmann::json& markEntry : *marks)
				{
					const std::optional<DecodedMark> mark = DecodeMark(markEntry);

					if (mark.has_value())
					{
						session.Marks.insert_or_assign(mark->Key, mark->Mark);
					}
				}
			}

			return session;
		}
	}

	const ReviewSession* ReviewSessions::Find(const std::string& branch) const
	{
		const Sessions::const_iterator found = m_Sessions.find(branch);

		if (found == m_Sessions.end())
		{
			return nullptr;
		}

		const auto& [name, session] = *found;
		return &session;
	}

	void ReviewSessions::Start(const std::string& branch, const std::string& base)
	{
		const auto [entry, inserted] = m_Sessions.try_emplace(branch, ReviewSession{.Base = base});

		if (inserted)
		{
			++m_Version;
		}
	}

	void ReviewSessions::End(const std::string& branch)
	{
		if (m_Sessions.erase(branch) > 0)
		{
			m_MissingLastTime.erase(branch);
			++m_Version;
		}
	}

	void ReviewSessions::SetSettings(const std::string& branch, const SessionSettings& settings)
	{
		ReviewSession* session = SessionNamed(m_Sessions, branch);

		if (session == nullptr || session->Settings == settings)
		{
			return;
		}

		session->Settings = settings;
		++m_Version;
	}

	ViewedState ReviewSessions::StateOf(const std::string& branch, ReviewGroup group, const Core::FileDiff& file) const
	{
		const ReviewSession* session = Find(branch);
		return session == nullptr ? ViewedState::NotViewed : StateIn(session->Marks, group, file);
	}

	std::optional<ViewedMark> ReviewSessions::MarkOf(const std::string& branch, ReviewGroup group,
													 const Core::FileDiff& file) const
	{
		const ReviewSession* session = Find(branch);

		if (session == nullptr)
		{
			return std::nullopt;
		}

		const BranchMarks::const_iterator found = session->Marks.find(MarkKey{.Group = group, .Path = ShownPath(file)});

		if (found == session->Marks.end())
		{
			return std::nullopt;
		}

		const auto& [key, mark] = *found;
		return mark;
	}

	void ReviewSessions::Mark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file)
	{
		ReviewSession* session = SessionNamed(m_Sessions, branch);

		if (session == nullptr)
		{
			return;
		}

		session->Marks.insert_or_assign(MarkKey{.Group = group, .Path = ShownPath(file)},
										ViewedMark{.Old = file.OldId, .New = file.NewId});
		++m_Version;
	}

	void ReviewSessions::Unmark(const std::string& branch, ReviewGroup group, const Core::FileDiff& file)
	{
		ReviewSession* session = SessionNamed(m_Sessions, branch);

		if (session == nullptr)
		{
			return;
		}

		if (session->Marks.erase(MarkKey{.Group = group, .Path = ShownPath(file)}) > 0)
		{
			++m_Version;
		}
	}

	void ReviewSessions::Prune(const std::vector<Core::Branch>& branches)
	{
		std::set<std::string> missingNow;
		std::vector<std::string> gone;

		for (const auto& [branch, session] : m_Sessions)
		{
			if (ListsBranch(branches, branch))
			{
				continue;
			}

			if (m_MissingLastTime.contains(branch))
			{
				gone.push_back(branch);
			}
			else
			{
				missingNow.insert(branch);
			}
		}

		for (const std::string& branch : gone)
		{
			m_Sessions.erase(branch);
		}

		m_MissingLastTime = std::move(missingNow);

		if (!gone.empty())
		{
			++m_Version;
		}
	}

	void ReviewSessions::Rename(const std::string& from, const std::string& to)
	{
		bool changed = false;
		Sessions::node_type moved = m_Sessions.extract(from);

		if (!moved.empty())
		{
			m_Sessions.insert_or_assign(to, std::move(moved.mapped()));
			m_MissingLastTime.erase(from);
			changed = true;
		}

		for (auto& [branch, session] : m_Sessions)
		{
			if (session.Base == from)
			{
				session.Base = to;
				changed = true;
			}
		}

		if (changed)
		{
			++m_Version;
		}
	}

	void ReviewSessions::Replace(Sessions sessions)
	{
		m_Sessions = std::move(sessions);
		m_MissingLastTime.clear();
		++m_Version;
	}

	std::optional<SinceViewedRequest> SinceViewedRequestFor(const ReviewSessions& sessions, const std::string& branch,
															ReviewGroup group, const Core::FileDiff& file,
															const ReviewRange& range, Core::WhitespaceInDiff whitespace)
	{
		const bool hasText = file.Change != Core::FileChange::Deleted && !file.IsBinary;

		if (!hasText || sessions.StateOf(branch, group, file) != ViewedState::ChangedSinceViewed)
		{
			return std::nullopt;
		}

		const std::optional<ViewedMark> mark = sessions.MarkOf(branch, group, file);

		if (!mark.has_value() || mark->New == file.NewId)
		{
			return std::nullopt;
		}

		SinceViewedRequest request{.ViewedBlob = mark->New,
								   .CurrentBlob = file.NewId,
								   .Path = ShownPath(file),
								   .Whitespace = whitespace};

		switch (group)
		{
		case ReviewGroup::Commits:
			request.Tip = range.Ends.Tip;
			return request;
		case ReviewGroup::Uncommitted:
			if (!range.Worktree.has_value())
			{
				return std::nullopt;
			}

			request.Worktree = range.Worktree;
			return request;
		}

		return std::nullopt;
	}

	std::string EncodeReviewSessions(const ReviewSessions::Sessions& sessions)
	{
		nlohmann::json encoded = nlohmann::json::object();

		for (const auto& [branch, session] : sessions)
		{
			encoded[branch] = EncodeSession(session);
		}

		const nlohmann::json document{{kVersionKey, kFormatVersion}, {kSessionsKey, std::move(encoded)}};

		return document.dump(kIndentWidth, kIndentCharacter, kKeepNonAscii, nlohmann::json::error_handler_t::replace);
	}

	DecodedSessions DecodeReviewSessionsText(std::string_view json)
	{
		const nlohmann::json::parser_callback_t noCallback = nullptr;
		const bool allowExceptions = false;
		const nlohmann::json document = nlohmann::json::parse(json, noCallback, allowExceptions);

		if (document.is_discarded() || !document.is_object())
		{
			return DecodedSessions{.Text = ReviewsText::Malformed};
		}

		const nlohmann::json::const_iterator version = document.find(kVersionKey);

		if (version == document.end() || !version->is_number_integer())
		{
			return DecodedSessions{.Text = ReviewsText::Malformed};
		}

		if (version->get<std::int64_t>() != kFormatVersion)
		{
			return DecodedSessions{.Text = ReviewsText::OtherVersion};
		}

		const nlohmann::json::const_iterator sessions = document.find(kSessionsKey);

		if (sessions == document.end() || !sessions->is_object())
		{
			return DecodedSessions{.Text = ReviewsText::Malformed};
		}

		DecodedSessions decoded;

		for (const auto& [branch, entry] : sessions->items())
		{
			std::optional<ReviewSession> session = DecodeSession(entry);

			if (session.has_value())
			{
				decoded.Sessions[branch] = std::move(*session);
			}
		}

		return decoded;
	}
}
