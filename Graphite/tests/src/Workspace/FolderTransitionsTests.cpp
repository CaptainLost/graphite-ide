#include <doctest/doctest.h>

#include "Workspace/FolderTransitions.h"
#include "Workspace/WorkspaceTrust.h"

#include "Core/Base/Base.h"
#include "Core/FileSystem/FileSystem.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

	using Graphite::FolderArrival;
	using Graphite::FolderExit;
	using Graphite::FolderTransitions;
	using Graphite::WorkspaceState;

	std::string NameOf(FolderExit exit)
	{
		return exit == FolderExit::Replaced ? "Replaced" : "Closed";
	}

	// Writes every step it is asked to take into a log shared with the other participants of the test.
	class RecordingParticipant final : public Graphite::FolderParticipant
	{
	public:
		RecordingParticipant(std::string name, std::vector<std::string>& log) : m_Name(std::move(name)), m_Log(log) {}

		void LetGo(FolderExit exit) override { m_Log.push_back(m_Name + " lets go, " + NameOf(exit)); }

		void Arrive(const FolderArrival& arrival) override
		{
			m_Log.push_back(m_Name + " arrives at " + Core::ToUtf8(arrival.Root));
			Arrived = arrival;
		}

		void FollowMove(const std::filesystem::path&, const std::filesystem::path& to) override
		{
			m_Log.push_back(m_Name + " follows to " + Core::ToUtf8(to));

			if (Trust != nullptr)
			{
				TrustedWhenFollowing = Trust->IsTrusted(to);
			}
		}

		void Capture(WorkspaceState& state) const override
		{
			if (CapturedTab.has_value())
			{
				state.Tabs.push_back({.Path = *CapturedTab});
			}

			if (CapturedDiff.has_value())
			{
				state.Diffs.push_back({.Path = *CapturedDiff});
			}
		}

		std::optional<FolderArrival> Arrived;

		const Graphite::WorkspaceTrust* Trust = nullptr;
		std::optional<bool> TrustedWhenFollowing;

		std::optional<std::filesystem::path> CapturedTab;
		std::optional<std::filesystem::path> CapturedDiff;

	private:
		std::string m_Name;
		std::vector<std::string>& m_Log;
	};

	WorkspaceState NothingSaved(const std::filesystem::path&)
	{
		return {};
	}
}

TEST_CASE("Opening a folder lets every participant go of the last one before any of them arrives")
{
	std::vector<std::string> log;
	Graphite::WorkspaceTrust trust;
	FolderTransitions transitions{trust, NothingSaved};
	transitions.Add(Core::CreateScope<RecordingParticipant>("first", log));
	transitions.Add(Core::CreateScope<RecordingParticipant>("second", log));

	transitions.Open("D:\\Work");

	const std::vector<std::string> expected{"first lets go, Replaced", "second lets go, Replaced",
											"first arrives at D:\\Work", "second arrives at D:\\Work"};
	CHECK(log == expected);
}

TEST_CASE("Closing the folder lets every participant go and brings nothing back")
{
	std::vector<std::string> log;
	Graphite::WorkspaceTrust trust;
	FolderTransitions transitions{trust, NothingSaved};
	transitions.Add(Core::CreateScope<RecordingParticipant>("first", log));
	transitions.Add(Core::CreateScope<RecordingParticipant>("second", log));

	transitions.Close();

	const std::vector<std::string> expected{"first lets go, Closed", "second lets go, Closed"};
	CHECK(log == expected);
}

TEST_CASE("A move reaches every participant in order, with the folder's trust already moved")
{
	std::vector<std::string> log;
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Repos\\IDE");
	FolderTransitions transitions{trust, NothingSaved};

	Core::Scope<RecordingParticipant> first = Core::CreateScope<RecordingParticipant>("first", log);
	first->Trust = &trust;
	RecordingParticipant& firstSeen = *first;
	transitions.Add(std::move(first));
	transitions.Add(Core::CreateScope<RecordingParticipant>("second", log));

	transitions.Move("D:\\Repos\\IDE", "D:\\Repos\\Graphite");

	const std::vector<std::string> expected{"first follows to D:\\Repos\\Graphite", "second follows to D:\\Repos\\Graphite"};
	CHECK(log == expected);
	CHECK(firstSeen.TrustedWhenFollowing == true);
	CHECK_FALSE(trust.IsTrusted("D:\\Repos\\IDE"));
}

TEST_CASE("An untrusted folder arrives with its saved state, keeping only the tabs inside it for restoring")
{
	std::vector<std::string> log;
	Graphite::WorkspaceTrust trust;

	WorkspaceState saved;
	saved.Tabs.push_back({.Path = "D:\\Work\\inside.cpp"});
	saved.Tabs.push_back({.Path = "D:\\Elsewhere\\outside.cpp"});

	FolderTransitions transitions{trust, [&saved](const std::filesystem::path&) { return saved; }};

	Core::Scope<RecordingParticipant> participant = Core::CreateScope<RecordingParticipant>("only", log);
	RecordingParticipant& seen = *participant;
	transitions.Add(std::move(participant));

	transitions.Open("D:\\Work");

	REQUIRE(seen.Arrived.has_value());
	CHECK_FALSE(seen.Arrived->Trusted);
	CHECK(seen.Arrived->Stored.Tabs.size() == 2);
	REQUIRE(seen.Arrived->Restorable.Tabs.size() == 1);
	CHECK(seen.Arrived->Restorable.Tabs.front().Path == std::filesystem::path("D:\\Work\\inside.cpp"));
}

TEST_CASE("A trusted folder arrives with all of its saved state to restore")
{
	std::vector<std::string> log;
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Work");

	WorkspaceState saved;
	saved.Tabs.push_back({.Path = "D:\\Elsewhere\\outside.cpp"});

	FolderTransitions transitions{trust, [&saved](const std::filesystem::path&) { return saved; }};

	Core::Scope<RecordingParticipant> participant = Core::CreateScope<RecordingParticipant>("only", log);
	RecordingParticipant& seen = *participant;
	transitions.Add(std::move(participant));

	transitions.Open("D:\\Work");

	REQUIRE(seen.Arrived.has_value());
	CHECK(seen.Arrived->Trusted);
	CHECK(seen.Arrived->Restorable.Tabs.size() == 1);
}

TEST_CASE("The folder's state to save is put together from every participant")
{
	std::vector<std::string> log;
	Graphite::WorkspaceTrust trust;
	FolderTransitions transitions{trust, NothingSaved};

	Core::Scope<RecordingParticipant> tabs = Core::CreateScope<RecordingParticipant>("tabs", log);
	tabs->CapturedTab = "D:\\Work\\a.cpp";
	transitions.Add(std::move(tabs));

	Core::Scope<RecordingParticipant> diffs = Core::CreateScope<RecordingParticipant>("diffs", log);
	diffs->CapturedDiff = "b.cpp";
	transitions.Add(std::move(diffs));

	const WorkspaceState captured = transitions.Capture();

	REQUIRE(captured.Tabs.size() == 1);
	CHECK(captured.Tabs.front().Path == std::filesystem::path("D:\\Work\\a.cpp"));
	REQUIRE(captured.Diffs.size() == 1);
	CHECK(captured.Diffs.front().Path == std::filesystem::path("b.cpp"));
}
