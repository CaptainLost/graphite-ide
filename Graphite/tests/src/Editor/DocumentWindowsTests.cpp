#include <doctest/doctest.h>

#include "Editor/DocumentWindows.h"
#include "Editor/EditorSettings.h"
#include "Git/Changes/ChangeBases.h"
#include "Image/ImageTabs.h"
#include "Language/Analysis/AnalysisStore.h"
#include "Language/Analysis/FileTextSource.h"
#include "Language/Analysis/WorkspaceAnalysis.h"
#include "Search/ReferencesPanel.h"
#include "Theme/ActiveScheme.h"
#include "Theme/BuiltInSchemes.h"
#include "UI/FileIcons.h"
#include "Workspace/Workspace.h"

#include "DeferredJobs.h"
#include "FakeImageTexture.h"
#include "ImGuiFrames.h"
#include "ImageFixtures.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/ShortcutRouter.h"
#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

TEST_CASE("A save failure whose error names the file is shown as it is")
{
	const std::filesystem::path path("D:/Repos/IDE/src/a.cpp");

	CHECK(Graphite::SaveFailureMessage(path, "Cannot write 'D:/Repos/IDE/src/a.cpp': access denied") ==
		  "Cannot write 'D:/Repos/IDE/src/a.cpp': access denied");
}

TEST_CASE("A save failure whose error does not name the file is prefixed with its path")
{
	const std::filesystem::path path("D:/Repos/IDE/src/a.cpp");

	CHECK(Graphite::SaveFailureMessage(path, "cannot write 'D:/Temp/x1.tmp': disk full") ==
		  "D:/Repos/IDE/src/a.cpp: cannot write 'D:/Temp/x1.tmp': disk full");
	CHECK(Graphite::SaveFailureMessage(path, "access denied") == "D:/Repos/IDE/src/a.cpp: access denied");
}

TEST_CASE("A document's window name starts with its file kind's tab glyph, not its badge, and is identified by its path")
{
	TempDirectory folder;
	const std::filesystem::path notes = folder.MakeFile("README.md", "# Notes");

	Core::DocumentSet documents;
	REQUIRE(documents.Open(notes, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);

	constexpr std::size_t kOnlyDocument = 0;
	const std::string name = Graphite::DocumentWindowName(documents, kOnlyDocument);

	CHECK(name.starts_with(Graphite::GlyphOf(Graphite::FileKind::Markdown)));
	CHECK_FALSE(name.starts_with(Graphite::BadgeOf(Graphite::FileKind::Markdown)));
	CHECK(name.ends_with("###" + Core::ToUtf8(documents.At(kOnlyDocument).Path)));
}

namespace {
	using Graphite::LanguageId;
	using Graphite::LanguageServices;

	constexpr ImVec2 kDocumentWindowSize(600.0f, 400.0f);
	constexpr ImVec2 kBesideTheFirst(640.0f, 0.0f);
	constexpr std::size_t kAnalysisBudgetBytes = 1u << 20;
	constexpr ImVec2 kPictureSize(2.0f, 2.0f);

	template<std::size_t Count>
	std::string Contents(const std::uint8_t (&bytes)[Count])
	{
		return std::string(reinterpret_cast<const char*>(bytes), Count);
	}

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
	};

	class SilentTexts final : public Graphite::FileTextSource
	{
	public:
		void Request(const std::filesystem::path&) override {}
		void Take(std::vector<Graphite::FileText>&) override {}
	};

	// The document windows as the application draws them, without docking: every tab is a window of its own, placed
	// and sized here at the start of each frame. Picture loads wait in Jobs until the test runs them.
	struct DrawnDocuments
	{
		explicit DrawnDocuments(std::filesystem::path root) : Root(std::move(root))
		{
			Settings.RegisterCommands(Commands, Keys);
			Settings.SetPictureZoom([this](Graphite::PictureZoom zoom) { return Windows.ZoomFocusedPicture(zoom); });
		}

		void Frame(const std::function<void()>& beforeTheWindows = {})
		{
			Frames.Next([this, &beforeTheWindows] {
				PlaceWindows();

				if (beforeTheWindows)
				{
					beforeTheWindows();
				}

				Windows.Render(Graphite::CommandContext{.Commands = &Commands, .Keys = &Keys, .Router = &Router},
							   Graphite::EditorFrame{.FontSize = Settings.GetFontSize(),
													 .Indent = Settings.GetIndentStyle(),
													 .FallbackDockNode = 0,
													 .WordWrap = false,
													 .ChangeMarkers = {},
													 .Hover = Settings.GetHoverSettings(),
													 .Scheme = Graphite::DarkScheme(),
													 .CloseRequests = CloseRequests,
													 .WorkspaceRoot = Root,
													 .Settings = Settings},
							   References);
			});
			CloseRequests.EndFrame();
		}

		// The first frames lay the tab out and start the load, which lands in the frames after the job ran.
		void ShowLanded()
		{
			Frame();
			Frame();
			Jobs.RunAll();
			Frame();
			Frame();
		}

		void PlaceWindows()
		{
			for (std::size_t index = 0; index < Documents.GetCount(); ++index)
			{
				const std::string name = Graphite::DocumentWindowName(Documents, index);

				if (ImGui::FindWindowByName(name.c_str()) == nullptr)
				{
					continue;
				}

				const auto place = Places.find(Documents.At(index).Path);
				ImGui::SetWindowPos(name.c_str(), place != Places.end() ? place->second : ImVec2(0.0f, 0.0f));
				ImGui::SetWindowSize(name.c_str(), kDocumentWindowSize);
			}
		}

		void Focus(const std::filesystem::path& path)
		{
			const std::optional<std::size_t> index = Documents.Find(path);
			REQUIRE(index.has_value());
			const std::string name = Graphite::DocumentWindowName(Documents, *index);

			Frame([&name] { ImGui::SetWindowFocus(name.c_str()); });
			Frame();
		}

		Graphite::ImageTab& Picture(const std::filesystem::path& path) { return Images.For(path); }

		// Ctrl held and the wheel turned one notch away from the user, over `at`.
		void CtrlWheelUpAt(ImVec2 at)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddMousePosEvent(at.x, at.y);
			Frame();
			io.AddKeyEvent(ImGuiMod_Ctrl, true);
			io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
			Frame();
			io.AddMouseWheelEvent(0.0f, 1.0f);
			Frame();
			io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			Frame();
		}

		ImGuiFrames Frames;
		std::filesystem::path Root;
		TempAppSettings Store;
		Core::EventBus Events;
		Graphite::Workspace Workspace{Events, Store.Get()};
		Core::DocumentSet Documents;
		Graphite::ModalGate Modals;
		SilentServer Server;
		Graphite::DiagnosticsModel Diagnostics;
		NoBases Bases;
		DeferredJobs MarkerJobs;
		Graphite::ChangeMarkersModel ChangeMarkers{Bases, MarkerJobs.Submitter()};
		SilentPreviewPort PreviewPort;
		Graphite::PreviewSemantics Preview{PreviewPort};
		Graphite::AnalysisCache Analysis{kAnalysisBudgetBytes};
		Graphite::AnalysisStore AnalysisStore;
		SilentAnalysisPort AnalysisPort;
		SilentTexts Texts;
		Graphite::WorkspaceAnalysis Pass{AnalysisPort, Texts, Analysis, &AnalysisStore};
		Graphite::ActiveEditorTarget ActiveEditor;
		DeferredJobs Jobs;
		FakeTextureFactory Textures;
		Graphite::ImageTabs Images{Jobs.Submitter(), Textures};
		Graphite::DocumentWindows Windows{Documents, Modals, Server, Diagnostics, ChangeMarkers, Preview, Analysis,
										  AnalysisStore, Pass, Events, ActiveEditor, Images};
		Graphite::EditorSettings Settings{Store.Get()};
		Graphite::ActiveScheme Scheme;
		Graphite::ReferencesPanel References{Workspace, Documents, Scheme, Settings, Preview};
		Core::TabCloseRequests CloseRequests;
		Core::CommandRegistry Commands;
		Core::Keymap Keys;
		Core::ShortcutRouter Router;
		std::map<std::filesystem::path, ImVec2> Places;
	};

	ImVec2 MiddleOfWindowAt(ImVec2 place)
	{
		return ImVec2(place.x + kDocumentWindowSize.x * 0.5f, place.y + kDocumentWindowSize.y * 0.5f);
	}
}

TEST_CASE("An image tab is kept across a restart")
{
	TempDirectory folder;
	const std::filesystem::path picture = folder.MakeFile("logo.png", Contents(ImageFixtures::kPng2x2));
	const std::filesystem::path preview = folder.MakeFile("icon.png", Contents(ImageFixtures::kPng2x2));

	Graphite::WorkspaceState state;

	{
		DrawnDocuments before(folder.GetPath());
		before.Windows.RequestOpen(preview, false);
		before.Windows.RequestOpen(picture, true);

		state = before.Windows.CaptureState();
	}

	REQUIRE(state.Tabs.size() == 2);
	CHECK(state.Tabs[0].Path == preview);
	CHECK(state.Tabs[0].Preview);
	CHECK(state.Tabs[1].Path == picture);
	CHECK_FALSE(state.Tabs[1].Preview);
	CHECK(state.Active == std::optional<std::filesystem::path>{picture});

	DrawnDocuments after(folder.GetPath());
	after.Windows.RestoreState(state);

	REQUIRE(after.Documents.GetCount() == 2);

	const std::optional<std::size_t> restored = after.Documents.Find(picture);
	REQUIRE(restored.has_value());
	CHECK(after.Documents.At(*restored).View == Core::ItemView::Image);
	CHECK(after.Documents.At(*restored).Failure == Core::OpenFailure::None);
	CHECK_FALSE(after.Documents.At(*restored).Preview);
	CHECK(after.Documents.GetActiveIndex() == restored);

	const std::optional<std::size_t> restoredPreview = after.Documents.Find(preview);
	REQUIRE(restoredPreview.has_value());
	CHECK(after.Documents.At(*restoredPreview).Preview);
}

TEST_CASE("Closing an image tab frees its picture")
{
	TempDirectory folder;
	const std::filesystem::path picture = folder.MakeFile("logo.png", Contents(ImageFixtures::kPng2x2));

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(picture, true);
	drawn.ShowLanded();

	REQUIRE(drawn.Picture(picture).GetState() == Graphite::ImageTabState::Ready);
	REQUIRE(drawn.Textures.GetLiveCount() == 1);

	CHECK(drawn.Windows.CanClose());
	drawn.Windows.Close();
	drawn.Frame();

	CHECK(drawn.Documents.GetCount() == 0);
	CHECK(drawn.Textures.GetLiveCount() == 0);
	CHECK(drawn.Windows.GetViewStatusItems().empty());
}

TEST_CASE("A changed image file reloads in its tab")
{
	TempDirectory folder;
	const std::filesystem::path picture = folder.MakeFile("logo.png", Contents(ImageFixtures::kPng2x2));

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(picture, true);
	drawn.ShowLanded();

	REQUIRE(drawn.Windows.GetViewStatusItems() ==
			std::vector<Graphite::ViewStatusItem>{{"2 × 2 · PNG · 77 B", false, std::nullopt}});

	const std::filesystem::file_time_type before = std::filesystem::last_write_time(picture);
	folder.MakeFile("logo.png", Contents(ImageFixtures::kGrayAlphaPng2x1));
	std::filesystem::last_write_time(picture, before + std::chrono::seconds(60));

	SUBCASE("The workspace reports the change")
	{
		drawn.Windows.HandleFilesChanged({folder.GetPath()}, false);
	}

	SUBCASE("The application comes back to the front")
	{
		drawn.Windows.NotifyWindowFocus(false);
		drawn.Windows.NotifyWindowFocus(true);
	}

	REQUIRE(drawn.Jobs.Count() == 1);
	drawn.Jobs.RunAll();
	drawn.Frame();
	drawn.Frame();

	CHECK(drawn.Windows.GetViewStatusItems() ==
		  std::vector<Graphite::ViewStatusItem>{{"2 × 1 · PNG · 70 B", false, std::nullopt}});
}

TEST_CASE("Ctrl+= zooms a focused image instead of the font")
{
	TempDirectory folder;
	const std::filesystem::path picture = folder.MakeFile("logo.png", Contents(ImageFixtures::kPng2x2));

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(picture, true);
	drawn.ShowLanded();

	const float font = drawn.Settings.GetFontSize();
	REQUIRE(drawn.Picture(picture).GetZoom().Zoom == 1.0f);

	CHECK(drawn.Commands.Run("editor.zoomIn"));
	CHECK(drawn.Picture(picture).GetZoom().Zoom == 1.5f);
	CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font));

	CHECK(drawn.Commands.Run("editor.zoomOut"));
	CHECK(drawn.Picture(picture).GetZoom().Zoom == 1.0f);
	CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font));
}

TEST_CASE("Ctrl+0 fits a focused image")
{
	TempDirectory folder;
	const std::filesystem::path picture = folder.MakeFile("logo.png", Contents(ImageFixtures::kPng2x2));

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(picture, true);
	drawn.ShowLanded();

	const float font = drawn.Settings.GetFontSize();
	drawn.Picture(picture).GetZoom().Pan = ImVec2(3.0f, 2.0f);

	CHECK(drawn.Commands.Run("editor.resetZoom"));

	// A picture of two pixels fits far above 100%, and Fit puts it in the middle.
	const Graphite::ZoomState fitted = drawn.Picture(picture).GetZoom();
	CHECK(fitted.Zoom > 100.0f);
	CHECK(fitted.Pan.x == 0.0f);
	CHECK(fitted.Pan.y == 0.0f);
	CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font));

	drawn.Frame();
	CHECK(drawn.Commands.Run("editor.zoomOut"));
	CHECK(drawn.Commands.Run("editor.resetZoom"));
	CHECK(drawn.Picture(picture).GetZoom().Zoom == doctest::Approx(fitted.Zoom));
}

TEST_CASE("Ctrl+= in a text tab still changes the font")
{
	TempDirectory folder;
	const std::filesystem::path picture = folder.MakeFile("logo.png", Contents(ImageFixtures::kPng2x2));
	const std::filesystem::path notes = folder.MakeFile("notes.txt", "some notes\n");

	DrawnDocuments drawn(folder.GetPath());
	drawn.Places[notes] = kBesideTheFirst;
	drawn.Windows.RequestOpen(picture, true);
	drawn.Windows.RequestOpen(notes, true);
	drawn.ShowLanded();
	drawn.Focus(notes);

	const float font = drawn.Settings.GetFontSize();
	const float zoom = drawn.Picture(picture).GetZoom().Zoom;

	CHECK(drawn.Commands.Run("editor.zoomIn"));

	CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font + 1.0f));
	CHECK(drawn.Picture(picture).GetZoom().Zoom == zoom);
	CHECK(drawn.Windows.GetViewStatusItems().empty());
}

TEST_CASE("Ctrl+wheel over a picture zooms the picture and not the font")
{
	TempDirectory folder;
	const std::filesystem::path picture = folder.MakeFile("logo.png", Contents(ImageFixtures::kPng2x2));

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(picture, true);
	drawn.ShowLanded();

	const float font = drawn.Settings.GetFontSize();
	REQUIRE(drawn.Picture(picture).GetZoom().Zoom == 1.0f);

	drawn.CtrlWheelUpAt(MiddleOfWindowAt(ImVec2(0.0f, 0.0f)));

	CHECK(drawn.Picture(picture).GetZoom().Zoom == 1.5f);
	CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font));
}

TEST_CASE("Ctrl+wheel over a text editor changes the font while a picture holds the focus")
{
	TempDirectory folder;
	const std::filesystem::path picture = folder.MakeFile("logo.png", Contents(ImageFixtures::kPng2x2));
	const std::filesystem::path notes = folder.MakeFile("notes.txt", "some notes\n");

	DrawnDocuments drawn(folder.GetPath());
	drawn.Places[notes] = kBesideTheFirst;
	drawn.Windows.RequestOpen(notes, true);
	drawn.Windows.RequestOpen(picture, true);
	drawn.ShowLanded();
	drawn.Focus(picture);

	const float font = drawn.Settings.GetFontSize();
	REQUIRE(drawn.Picture(picture).GetZoom().Zoom == 1.0f);

	drawn.CtrlWheelUpAt(MiddleOfWindowAt(kBesideTheFirst));

	CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font + 1.0f));
	CHECK(drawn.Picture(picture).GetZoom().Zoom == 1.0f);
}
