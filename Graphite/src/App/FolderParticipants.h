#pragma once

#include "Workspace/FolderTransitions.h"

#include "Core/Base/Base.h"
#include "Core/Git/Ref.h"

#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

namespace Core {
	class DocumentSet;
}

namespace Graphite {

	class DiffWindows;
	class DocumentWindows;
	class GitView;
	class LanguageServices;
	class RepositoryGitPort;
	class ReviewModel;
	class ReviewProgress;
	class ReviewStore;
	class ReviewWindows;
	class SessionWindows;
	class StateKeeper;
	class TerminalWindows;

	// The root the language servers fall back to for a file with no marker above it.
	class ServerRootOfFolder final : public FolderParticipant
	{
	public:
		explicit ServerRootOfFolder(LanguageServices& services) : m_Services(services) {}

		void LetGo(FolderExit exit) override;
		void Arrive(const FolderArrival& arrival) override;
		void FollowMove(const std::filesystem::path& from, const std::filesystem::path& to) override;
		void Capture(WorkspaceState& state) const override;

	private:
		LanguageServices& m_Services;
	};

	class SavedStateOfFolder final : public FolderParticipant
	{
	public:
		explicit SavedStateOfFolder(StateKeeper& keeper) : m_Keeper(keeper) {}

		void LetGo(FolderExit exit) override;
		void Arrive(const FolderArrival& arrival) override;
		void FollowMove(const std::filesystem::path& from, const std::filesystem::path& to) override;
		void Capture(WorkspaceState& state) const override;

	private:
		StateKeeper& m_Keeper;
	};

	class DocumentsOfFolder final : public FolderParticipant
	{
	public:
		DocumentsOfFolder(DocumentWindows& windows, Core::DocumentSet& documents)
			: m_Windows(windows), m_Documents(documents)
		{
		}

		void LetGo(FolderExit exit) override;
		void Arrive(const FolderArrival& arrival) override;
		void FollowMove(const std::filesystem::path& from, const std::filesystem::path& to) override;
		void Capture(WorkspaceState& state) const override;

	private:
		DocumentWindows& m_Windows;
		Core::DocumentSet& m_Documents;
	};

	class SessionsOfFolder final : public FolderParticipant
	{
	public:
		explicit SessionsOfFolder(SessionWindows& windows) : m_Windows(windows) {}

		void LetGo(FolderExit exit) override;
		void Arrive(const FolderArrival& arrival) override;
		void FollowMove(const std::filesystem::path& from, const std::filesystem::path& to) override;
		void Capture(WorkspaceState& state) const override;

	private:
		SessionWindows& m_Windows;
	};

	class TerminalsOfFolder final : public FolderParticipant
	{
	public:
		explicit TerminalsOfFolder(TerminalWindows& terminals) : m_Terminals(terminals) {}

		void LetGo(FolderExit exit) override;
		void Arrive(const FolderArrival& arrival) override;
		void FollowMove(const std::filesystem::path& from, const std::filesystem::path& to) override;
		void Capture(WorkspaceState& state) const override;

	private:
		TerminalWindows& m_Terminals;
	};

	class GitWindowOfFolder final : public FolderParticipant
	{
	public:
		explicit GitWindowOfFolder(GitView& view) : m_View(view) {}

		void LetGo(FolderExit exit) override;
		void Arrive(const FolderArrival& arrival) override;
		void FollowMove(const std::filesystem::path& from, const std::filesystem::path& to) override;
		void Capture(WorkspaceState& state) const override;

	private:
		GitView& m_View;
	};

	// The port's root, whose old repository handles are let go of on the write worker, since that worker keeps its own.
	class RepositoryOfFolder final : public FolderParticipant
	{
	public:
		RepositoryOfFolder(RepositoryGitPort& port, std::function<void()> letGoOfOldHandles)
			: m_Port(port), m_LetGoOfOldHandles(std::move(letGoOfOldHandles))
		{
		}

		void LetGo(FolderExit exit) override;
		void Arrive(const FolderArrival& arrival) override;
		void FollowMove(const std::filesystem::path& from, const std::filesystem::path& to) override;
		void Capture(WorkspaceState& state) const override;

	private:
		void PointAt(const std::filesystem::path& root);

		RepositoryGitPort& m_Port;
		std::function<void()> m_LetGoOfOldHandles;
	};

	class DiffsOfFolder final : public FolderParticipant
	{
	public:
		explicit DiffsOfFolder(DiffWindows& windows) : m_Windows(windows) {}

		void LetGo(FolderExit exit) override;
		void Arrive(const FolderArrival& arrival) override;
		void FollowMove(const std::filesystem::path& from, const std::filesystem::path& to) override;
		void Capture(WorkspaceState& state) const override;

	private:
		DiffWindows& m_Windows;
	};

	struct ReviewParts
	{
		ReviewWindows& Windows;
		ReviewModel& Model;
		ReviewProgress& Progress;
		ReviewStore& Store;
		Core::Ref<const std::vector<Core::Branch>>& ReviewedBranches;
	};

	class ReviewsOfFolder final : public FolderParticipant
	{
	public:
		explicit ReviewsOfFolder(ReviewParts parts) : m_Parts(parts) {}

		void LetGo(FolderExit exit) override;
		void Arrive(const FolderArrival& arrival) override;
		void FollowMove(const std::filesystem::path& from, const std::filesystem::path& to) override;
		void Capture(WorkspaceState& state) const override;

	private:
		ReviewParts m_Parts;
	};
}
