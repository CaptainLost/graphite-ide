#include <doctest/doctest.h>

#include "Editor/DocumentWindows.h"
#include "Editor/EditorCommandTarget.h"
#include "Editor/Overlays/AnalysisCache.h"
#include "Editor/Server/DocumentServerPort.h"
#include "Editor/Server/PreviewSemantics.h"
#include "Git/Blame/BlameModel.h"
#include "Git/Changes/ChangeBases.h"
#include "Git/Changes/ChangeMarkersModel.h"
#include "Git/Changes/ChangesPanel.h"
#include "Git/Changes/ChangesShared.h"
#include "Git/Changes/ChangesView.h"
#include "Git/Diff/DiffWindows.h"
#include "Git/GitModel.h"
#include "Image/ImageTabs.h"
#include "Language/Analysis/AnalysisStore.h"
#include "Language/Analysis/FileTextSource.h"
#include "Language/Analysis/WorkspaceAnalysis.h"
#include "Language/Diagnostics/DiagnosticsModel.h"
#include "Theme/ActiveScheme.h"
#include "UI/Dialogs/ModalGate.h"

#include "FakeImageTexture.h"
#include "GitFixtures.h"
#include "HoveredItems.h"
#include "ImGuiFrames.h"

#include "Core/Events/EventBus.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
	using Graphite::LanguageId;
	using Graphite::LanguageServices;

	constexpr std::size_t kAnalysisBudgetBytes = 1u << 20;
	constexpr ImVec2 kHostSize(480.0f, 600.0f);
	constexpr float kIntoTheRow = 40.0f;
	constexpr int kOnlyEntry = 0;

	class OneModifiedFile final : public EmptyGitPort
	{
	public:
		Core::Result<Core::WorkingTreeStatus> QueryStatus() override
		{
			Core::FileStatus file;
			file.Path = "a.txt";
			file.WorkTree = Core::StatusFlags::Modified;
			return WorkingTreeStatusOf({file});
		}
	};

	class SilentServer final : public Graphite::DocumentServerPort
	{
	public:
		void EnsureStarted(const std::filesystem::path&, LanguageId) override {}
		void OpenDocument(const std::filesystem::path&, LanguageId, std::string) override {}
		void ChangeDocument(const std::filesystem::path&, LanguageId, std::string) override {}
		void SaveDocument(const std::filesystem::path&, LanguageId) override {}
		void CloseDocument(const std::filesystem::path&, LanguageId) override {}
		Graphite::ServerStatus StatusFor(const std::filesystem::path&, LanguageId) override { return {}; }
		bool HasChosenServer(LanguageId) override { return false; }
		bool OffersSwitchSourceHeader(LanguageId) override { return false; }
		LanguageServices::Ticket AskDefinition(const std::filesystem::path&, LanguageId, const Graphite::FilePosition&) override { return {}; }
		LanguageServices::Ticket AskReferences(const std::filesystem::path&, LanguageId, const Graphite::FilePosition&) override { return {}; }
		LanguageServices::Ticket AskHover(const std::filesystem::path&, LanguageId, const Graphite::FilePosition&) override { return {}; }
		LanguageServices::Ticket AskSwitchSourceHeader(const std::filesystem::path&, LanguageId) override { return {}; }
		LanguageServices::Ticket AskSymbols(const std::filesystem::path&, LanguageId) override { return {}; }
		LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path&, LanguageId) override { return {}; }
		void Cancel(LanguageServices::Ticket) override {}
		void TakeAnswers(std::vector<LanguageServices::Answer>&) override {}
		void TakeRecolour(std::vector<std::filesystem::path>&) override {}
	};

	class NoBases final : public Graphite::ChangeBasePort
	{
	public:
		Core::Result<Graphite::ChangeBases> ReadBases(const std::filesystem::path&) override
		{
			return Core::Error{Core::ErrorCode::NotFound, "not in a repository"};
		}
	};

	class NoBlame final : public Graphite::BlamePort
	{
	public:
		Core::Result<Graphite::HeadBlame> BlameAtHead(const std::filesystem::path&) override
		{
			return Graphite::HeadBlame{};
		}

		Core::Result<Core::LineHistory> QueryLineHistory(const Core::LineHistoryRequest&) override
		{
			return Core::Error{Core::ErrorCode::NotFound, "not in a repository"};
		}
	};

	class SilentPreviewPort final : public Graphite::PreviewServerPort
	{
	public:
		void Open(const std::filesystem::path&, LanguageId, std::string) override {}
		LanguageServices::Ticket Ask(const std::filesystem::path&, LanguageId) override { return {}; }
		void Close(const std::filesystem::path&, LanguageId) override {}
	};

	class SilentAnalysisPort final : public Graphite::WorkspaceAnalysisPort
	{
	public:
		void Open(const std::filesystem::path&, LanguageId, std::string) override {}
		LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path&, LanguageId) override { return {}; }
		LanguageServices::Ticket AskSymbols(const std::filesystem::path&, LanguageId) override { return {}; }
		void Close(const std::filesystem::path&, LanguageId) override {}
		Graphite::ServerReadiness ReadinessOf(const std::filesystem::path&, LanguageId) override { return Graphite::ServerReadiness::CannotRun; }
		bool HasChosenServer(LanguageId) override { return false; }
		std::string ServerNameOf(LanguageId) override { return {}; }
	};

	class SilentTexts final : public Graphite::FileTextSource
	{
	public:
		void Request(const std::filesystem::path&) override {}
		void Take(std::vector<Graphite::FileText>&) override {}
	};

	// The host's window, at the same place and size every frame. Each test names its own, because hover
	// animations are kept by id across tests.
	std::function<void()> InHost(const char* host, std::function<void()> body)
	{
		return [host, body = std::move(body)] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kHostSize);
			ImGui::Begin(host, nullptr, ImGuiWindowFlags_NoSavedSettings);
			body();
			ImGui::End();
		};
	}

	// What both hosts of the Changes view are made from, with a status of one modified file already landed.
	struct ChangesHosts
	{
		ChangesHosts()
		{
			Model.SetRoot("C:/repo");
			Model.RequestStatus();
			Model.OnUpdate(Core::Timestep{0.0f});
		}

		ImGuiFrames Frames;
		OneModifiedFile Port;
		Graphite::GitModel Model{Port, RunJobsInline()};
		Graphite::ModalGate Modals;
		Core::DocumentSet Documents;
		SilentServer Server;
		Graphite::DiagnosticsModel Diagnostics;
		NoBases Bases;
		Graphite::ChangeMarkersModel ChangeMarkers{Bases, [](std::function<void()>) {}};
		NoBlame BlameReader;
		Graphite::BlameModel Blame{BlameReader, [](std::function<void()>) {}};
		SilentPreviewPort PreviewPort;
		Graphite::PreviewSemantics Preview{PreviewPort};
		Graphite::AnalysisCache Analysis{kAnalysisBudgetBytes};
		Graphite::AnalysisStore AnalysisStore;
		SilentAnalysisPort AnalysisPort;
		SilentTexts Texts;
		Graphite::WorkspaceAnalysis Pass{AnalysisPort, Texts, Analysis, &AnalysisStore};
		Core::EventBus Events;
		Graphite::ActiveEditorTarget ActiveEditor;
		FakeTextureFactory Textures;
		Graphite::ImageTabs Images{[](std::function<void()>) {}, Textures};
		Graphite::DocumentWindows Windows{Documents, Modals, Server, Diagnostics, ChangeMarkers, Blame, Preview,
										  Analysis, AnalysisStore, Pass, Events, ActiveEditor, Images};
		Graphite::ActiveScheme Scheme;
		Graphite::ChangesShared Shared;
		Graphite::DiffWindows Diffs;
	};

	const ImGuiWindow* ListWindow()
	{
		constexpr std::string_view kListName = "changes.lists";

		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			const bool isTheList = (window->Flags & ImGuiWindowFlags_ChildWindow) != 0 &&
								   std::string_view{window->Name}.find(kListName) != std::string_view::npos;

			if (isTheList)
			{
				return window;
			}
		}

		return nullptr;
	}

	// The ids the only file of the CHANGES section is drawn under, read off the list's window as ImGui hashes them.
	ImGuiID FileRowScope(const ImGuiWindow& list)
	{
		const ImGuiID section = ImHashStr("CHANGES", 0, list.ID);
		return ImHashData(&kOnlyEntry, sizeof(kOnlyEntry), section);
	}

	ImGuiID FileRowId(const ImGuiWindow& list)
	{
		return ImHashStr("##row", 0, FileRowScope(list));
	}

	// ImGui keeps no item's position past its frame, so the pointer steps down the list, and the middle of the
	// stretch where the file row's selectable is under it is the row's. Nothing when it never is.
	std::optional<ImVec2> PointOnFileRow(ImGuiFrames& frames, const std::function<void()>& draw)
	{
		frames.Next(draw);

		const ImGuiWindow* list = ListWindow();

		if (list == nullptr)
		{
			return std::nullopt;
		}

		const ImGuiID rowId = FileRowId(*list);
		const float x = list->Pos.x + kIntoTheRow;

		for (const HoveredStretch& stretch : HoveredDown(frames, x, list->Pos.y, list->Pos.y + list->Size.y, draw))
		{
			if (stretch.Id == rowId)
			{
				return ImVec2(x, stretch.Middle());
			}
		}

		return std::nullopt;
	}

	bool ClickFileRow(ImGuiFrames& frames, const std::function<void()>& draw)
	{
		const std::optional<ImVec2> onTheRow = PointOnFileRow(frames, draw);

		if (!onTheRow.has_value())
		{
			return false;
		}

		frames.Click(*onTheRow, draw);
		return true;
	}

	// The row shows its buttons only while the pointer is over it, so they are looked for across the row.
	bool ClickFileRowDiscard(ImGuiFrames& frames, const std::function<void()>& draw)
	{
		const std::optional<ImVec2> onTheRow = PointOnFileRow(frames, draw);
		const ImGuiWindow* list = ListWindow();

		if (!onTheRow.has_value() || list == nullptr)
		{
			return false;
		}

		const ImGuiID discardId = ImHashStr("##discard", 0, FileRowScope(*list));
		const float listLeft = list->Pos.x;
		const float listRight = list->Pos.x + list->Size.x;

		for (const HoveredStretch& stretch : HoveredAcross(frames, onTheRow->y, listLeft, listRight, draw))
		{
			if (stretch.Id == discardId)
			{
				frames.Click(ImVec2(stretch.Middle(), onTheRow->y), draw);
				return true;
			}
		}

		return false;
	}
}

TEST_CASE("Picking a file hands its diff to the host")
{
	ChangesHosts hosts;
	Graphite::ChangesView view{hosts.Model, hosts.Modals, hosts.Documents, hosts.Windows,
							   hosts.Scheme, hosts.Events, hosts.Shared};

	std::vector<Graphite::OpenDiff> chosen;
	view.OnFileChosen = [&chosen](const Graphite::OpenDiff& diff) { chosen.push_back(diff); };

	const std::function<void()> draw = InHost("Picking host", [&view] { view.Draw(); });

	REQUIRE(ClickFileRow(hosts.Frames, draw));

	REQUIRE(chosen.size() == 1);
	CHECK(chosen.front().Path == std::filesystem::path("a.txt"));
	CHECK(chosen.front().Target == Core::DiffTarget::WorkTreeToIndex);
	CHECK_FALSE(chosen.front().Commit.has_value());
}

TEST_CASE("A commit clears the draft both hosts share")
{
	ChangesHosts hosts;
	Graphite::ChangesPanel panel{hosts.Model, hosts.Modals, hosts.Documents, hosts.Windows,
								 hosts.Scheme, hosts.Diffs, hosts.Events, hosts.Shared};

	hosts.Shared.Message = "fix";
	hosts.Shared.Amend = true;

	hosts.Model.RequestCommit({.Message = hosts.Shared.Message, .Amend = hosts.Shared.Amend});
	hosts.Model.OnUpdate(Core::Timestep{0.0f});

	panel.OnUpdate(Core::Timestep{0.0f});

	CHECK(hosts.Shared.Message.empty());
	CHECK_FALSE(hosts.Shared.Amend);
}

TEST_CASE("A view of its own leaves a landed commit for the side panel to take")
{
	ChangesHosts hosts;
	Graphite::ChangesView view{hosts.Model, hosts.Modals, hosts.Documents, hosts.Windows,
							   hosts.Scheme, hosts.Events, hosts.Shared};

	hosts.Shared.Message = "fix";

	hosts.Model.RequestCommit({.Message = hosts.Shared.Message});
	hosts.Model.OnUpdate(Core::Timestep{0.0f});

	hosts.Frames.Next(InHost("Own view host", [&view] { view.Draw(); }));

	CHECK(hosts.Shared.Message == "fix");
	CHECK(hosts.Model.TakeCommitted().has_value());
}

TEST_CASE("The side panel opens a diff tab for the picked file")
{
	ChangesHosts hosts;
	Graphite::ChangesPanel panel{hosts.Model, hosts.Modals, hosts.Documents, hosts.Windows,
								 hosts.Scheme, hosts.Diffs, hosts.Events, hosts.Shared};

	const std::function<void()> draw = InHost("Side panel host", [&panel] { panel.OnImGuiRender(); });

	REQUIRE(ClickFileRow(hosts.Frames, draw));

	CHECK(hosts.Diffs.GetCount() == 1);
	CHECK(hosts.Diffs.IsOpen(Graphite::OpenDiff{.Commit = std::nullopt,
												.Target = Core::DiffTarget::WorkTreeToIndex,
												.Path = "a.txt"}));
}

TEST_CASE("A side panel left undrawn hands back the modal gate its discard question held")
{
	ChangesHosts hosts;
	Graphite::ChangesPanel panel{hosts.Model, hosts.Modals, hosts.Documents, hosts.Windows,
								 hosts.Scheme, hosts.Diffs, hosts.Events, hosts.Shared};

	const std::function<void()> draw = InHost("Undrawn panel host", [&panel] {
		panel.OnImGuiRender();
		panel.SetDrawnOnLatestFrame(true);
	});

	REQUIRE(ClickFileRowDiscard(hosts.Frames, draw));
	hosts.Frames.Next(draw);

	REQUIRE(hosts.Modals.IsBusy());

	panel.OnUpdate(Core::Timestep{0.0f});
	CHECK(hosts.Modals.IsBusy());

	panel.SetDrawnOnLatestFrame(false);
	panel.OnUpdate(Core::Timestep{0.0f});
	CHECK_FALSE(hosts.Modals.IsBusy());
}
