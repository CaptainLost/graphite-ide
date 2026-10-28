#include <doctest/doctest.h>

#include "Editor/DocumentWindows.h"
#include "Editor/EditorCommandTarget.h"
#include "Editor/EditorCommands.h"
#include "Editor/EditorSettings.h"
#include "Editor/Navigation/SelectInDocument.h"
#include "Editor/Overlays/AnalysisCache.h"
#include "Editor/Server/DocumentServerPort.h"
#include "Editor/Server/PreviewSemantics.h"
#include "Git/Changes/ChangeBases.h"
#include "Git/Changes/ChangeMarkersModel.h"
#include "Hex/HexFiles.h"
#include "Image/ImageTabs.h"
#include "Language/Analysis/AnalysisStore.h"
#include "Language/Analysis/FileTextSource.h"
#include "Language/Analysis/WorkspaceAnalysis.h"
#include "Language/Diagnostics/DiagnosticsModel.h"
#include "Persistence/CaretRestore.h"
#include "Persistence/PersistedState.h"
#include "Search/ReferencesPanel.h"
#include "Theme/ActiveScheme.h"
#include "Theme/BuiltInSchemes.h"
#include "UI/Dialogs/ModalGate.h"
#include "UI/FileIcons.h"
#include "UI/ToastEvent.h"
#include "Workspace/Workspace.h"

#include "DeferredJobs.h"
#include "DrawnGlyphs.h"
#include "FakeImageTexture.h"
#include "FileFixtures.h"
#include "ImGuiFrames.h"
#include "ImageFixtures.h"
#include "SparseFile.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/App/Redraw.h"
#include "Core/Base/KeyCodes.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/ShortcutRouter.h"
#include "Core/Events/EventBus.h"
#include "Core/Events/FrameInput.h"
#include "Core/Events/KeyEvent.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"
#include "Core/Text/RowMap.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
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
	constexpr ImVec2 kMenuPlace(0.0f, 450.0f);
	constexpr const char* kMenuPopup = "##menu";
	constexpr ImVec2 kReferencePlace(0.0f, 600.0f);
	constexpr ImVec2 kReferenceSize(1280.0f, 100.0f);
	constexpr const char* kReferenceWindow = "Reference";
	constexpr const char* kUntouchedClipboard = "untouched";

	enum class Face
	{
		Interface,
		Code
	};

	// WARNING: A copy would otherwise replace whatever the person running the tests had on the system clipboard.
	void KeepClipboardText(ImGuiContext*, const char* text)
	{
		*static_cast<std::string*>(ImGui::GetPlatformIO().Platform_ClipboardUserData) = text;
	}

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
		std::string ServerNameOf(LanguageId) override { return {}; }
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
			Windows.RegisterCommands(Commands, Keys);
			Settings.SetPictureZoom([this](Graphite::PictureZoom zoom) { return Windows.ZoomFocusedPicture(zoom); });

			ImGui::GetPlatformIO().Platform_ClipboardUserData = &Clipboard;
			ImGui::GetPlatformIO().Platform_SetClipboardTextFn = &KeepClipboardText;
			Events.Subscribe<Graphite::ToastEvent>([this](const Graphite::ToastEvent& toast) { Toasts.push_back(toast); });
		}

		void Frame(const std::function<void()>& beforeTheWindows = {})
		{
			Frames.Next([this, &beforeTheWindows] {
				PlaceWindows();

				if (beforeTheWindows)
				{
					beforeTheWindows();
				}

				Windows.Render(Graphite::CommandContext{.Commands = &Commands, .Keys = &Keys, .Router = &Router, .Input = &Input},
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

		// As a click on the text does: the editor's surface, a child of the tab's window, takes the keyboard.
		void FocusText(const std::filesystem::path& path)
		{
			const std::optional<std::size_t> index = Documents.Find(path);
			REQUIRE(index.has_value());
			const std::string name = Graphite::DocumentWindowName(Documents, *index);
			bool found = false;

			Frame([&name, &found] {
				ImGuiWindow* const tab = ImGui::FindWindowByName(name.c_str());
				REQUIRE(tab != nullptr);

				for (ImGuiWindow* const child : tab->DC.ChildWindows)
				{
					if (std::string_view(child->Name).find("##EditorSurface") != std::string_view::npos)
					{
						ImGui::FocusWindow(child);
						found = true;
					}
				}
			});
			Frame();

			REQUIRE(found);
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

		// As the application takes a key: routed as Global when it arrives, then queued for whichever view holds the
		// keyboard in the frame that follows.
		void Press(Core::KeyCode key, Core::KeyModifier modifiers)
		{
			const Core::KeyPressedEvent pressed{key, modifiers, false};
			Router.OnKeyPressed(Commands, Keys, Core::ShortcutScope::Global, pressed);
			Input.PushKey(key, modifiers, false);
			Frame();
			Input.Clear();
		}

		// As a menu runs its item: from a popup that took the keyboard focus a frame earlier.
		bool RunFromMenu(const std::string& commandId)
		{
			bool ran = false;

			const auto menu = [this, &commandId, &ran](bool choose) {
				ImGui::SetNextWindowPos(kMenuPlace);
				ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

				if (!ImGui::IsPopupOpen(kMenuPopup))
				{
					ImGui::OpenPopup(kMenuPopup);
				}

				if (ImGui::BeginPopup(kMenuPopup))
				{
					if (choose)
					{
						ran = Commands.RunFromMenu(commandId);
						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}

				ImGui::End();
			};

			Frame([&menu] { menu(false); });
			Frame([&menu] { menu(true); });
			return ran;
		}

		// Whether the tab's window drew `text` below its title bar, matched glyph for glyph against the same text drawn in
		// a window of its own in the same frames, in the interface font or in the code face at the editor's size.
		bool TabDraws(const std::filesystem::path& path, const std::string& text, Face face)
		{
			const std::optional<std::size_t> index = Documents.Find(path);
			REQUIRE(index.has_value());
			const std::string name = Graphite::DocumentWindowName(Documents, *index);
			const float codeSize = Settings.GetFontSize();

			const auto reference = [&text, face, codeSize] {
				ImGui::SetNextWindowPos(kReferencePlace);
				ImGui::SetNextWindowSize(kReferenceSize);
				ImGui::Begin(kReferenceWindow, nullptr,
							 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
								 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs);

				if (face == Face::Code)
				{
					ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont(), codeSize);
				}

				ImGui::TextUnformatted(text.c_str());

				if (face == Face::Code)
				{
					ImGui::PopFont();
				}

				ImGui::End();
			};

			Frame(reference);
			Frame(reference);

			const ImGuiWindow* const tab = ImGui::FindWindowByName(name.c_str());
			REQUIRE(tab != nullptr);
			std::vector<DrawnGlyph> drawn = DrawnGlyphs(*tab->DrawList);
			std::erase_if(drawn, [tab](const DrawnGlyph& glyph) { return glyph.Box.Min.y < tab->InnerRect.Min.y; });
			const std::vector<DrawnGlyph> wanted = DrawnGlyphs(*ImGui::FindWindowByName(kReferenceWindow)->DrawList);
			REQUIRE_FALSE(wanted.empty());

			const auto sameGlyph = [](const DrawnGlyph& a, const DrawnGlyph& b) { return a.Uv.x == b.Uv.x && a.Uv.y == b.Uv.y; };
			return std::search(drawn.begin(), drawn.end(), wanted.begin(), wanted.end(), sameGlyph) != drawn.end();
		}

		// Extends the selection of the tab holding the keyboard by `bytes` to the right.
		void ShiftRight(int bytes)
		{
			for (int moved = 0; moved < bytes; ++moved)
			{
				Press(Core::KeyCode::Right, Core::KeyModifier::Shift);
			}
		}

		std::vector<Graphite::ViewStatusItem> StatusItems() const { return Windows.GetViewStatusItems(); }

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
		Graphite::HexFiles Hex{Jobs.Submitter()};
		Graphite::DocumentWindows Windows{Documents, Modals, Server, Diagnostics, ChangeMarkers, Preview, Analysis,
										  AnalysisStore, Pass, Events, ActiveEditor, Images, Hex};
		Graphite::EditorSettings Settings{Store.Get()};
		Graphite::ActiveScheme Scheme;
		Graphite::ReferencesPanel References{Workspace, Documents, Scheme, Settings, Preview};
		Core::TabCloseRequests CloseRequests;
		Core::CommandRegistry Commands;
		Core::Keymap Keys;
		Core::ShortcutRouter Router;
		Core::FrameInput Input;
		std::map<std::filesystem::path, ImVec2> Places;
		std::string Clipboard = kUntouchedClipboard;
		std::vector<Graphite::ToastEvent> Toasts;
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

TEST_CASE("A hex tab is kept across a restart")
{
	TempDirectory folder;
	const std::filesystem::path program = folder.MakeFile("program.exe", std::string("MZ\0\0binary", 10));

	Graphite::WorkspaceState state;

	{
		DrawnDocuments before(folder.GetPath());
		before.Windows.RequestOpen(program, true);

		state = before.Windows.CaptureState();
	}

	REQUIRE(state.Tabs.size() == 1);
	CHECK(state.Tabs[0].Path == program);

	DrawnDocuments after(folder.GetPath());
	after.Windows.RestoreState(state);

	REQUIRE(after.Documents.GetCount() == 1);
	CHECK(after.Documents.At(0).View == Core::ItemView::Hex);
	CHECK(after.Documents.At(0).Failure == Core::OpenFailure::None);

	after.ShowLanded();

	const std::vector<Graphite::ViewStatusItem> status = after.StatusItems();
	REQUIRE_FALSE(status.empty());
	CHECK(status.front().Text == "Offset 0x00000000");
	CHECK(status.back().Text == "10 B");
	CHECK(after.TabDraws(program, "00000000", Face::Code));
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

TEST_CASE("The close chord closes the image tab that holds the keyboard")
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

	REQUIRE(drawn.Picture(picture).GetState() == Graphite::ImageTabState::Ready);
	REQUIRE(drawn.Documents.GetCount() == 2);

	SUBCASE("Ctrl+W")
	{
		drawn.Press(Core::KeyCode::W, Core::KeyModifier::Control);
	}

	SUBCASE("Ctrl+F4")
	{
		drawn.Press(Core::KeyCode::F4, Core::KeyModifier::Control);
	}

	CHECK_FALSE(drawn.Documents.Find(picture).has_value());
	CHECK(drawn.Documents.Find(notes).has_value());
	CHECK(drawn.Textures.GetLiveCount() == 0);
}

TEST_CASE("Ctrl+= pressed in a focused image tab zooms its picture one step and leaves the font")
{
	TempDirectory folder;
	const std::filesystem::path picture = folder.MakeFile("logo.png", Contents(ImageFixtures::kPng2x2));

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(picture, true);
	drawn.ShowLanded();
	drawn.Focus(picture);

	const float font = drawn.Settings.GetFontSize();
	REQUIRE(drawn.Picture(picture).GetZoom().Zoom == 1.0f);

	drawn.Press(Core::KeyCode::Equal, Core::KeyModifier::Control);

	CHECK(drawn.Picture(picture).GetZoom().Zoom == 1.5f);
	CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font));
	CHECK(drawn.Documents.Find(picture).has_value());
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

TEST_CASE("Zoom In from a menu zooms the active image tab's picture and not the font")
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

	CHECK(drawn.RunFromMenu("editor.zoomIn"));

	CHECK(drawn.Picture(picture).GetZoom().Zoom == 1.5f);
	CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font));

	CHECK(drawn.RunFromMenu("editor.resetZoom"));

	CHECK(drawn.Picture(picture).GetZoom().Zoom > 100.0f);
	CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font));
}

TEST_CASE("Zoom In from a menu changes the font while a text tab is active")
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

	CHECK(drawn.RunFromMenu("editor.zoomIn"));

	CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font + 1.0f));
	CHECK(drawn.Picture(picture).GetZoom().Zoom == zoom);
}

namespace {

	// The start of a Windows executable, 4D 5A 90 00 03 00 00 00, then letters, so no row's offset is spelt among the
	// bytes after it.
	std::string ProgramBytes(std::size_t size)
	{
		std::string bytes("MZ\x90\0\x03\0\0\0", 8);

		for (std::size_t at = bytes.size(); at < size; ++at)
		{
			bytes += static_cast<char>('A' + at % 26);
		}

		return bytes;
	}

	// Moves the modified time on as well, so the change is seen whatever the clock's resolution.
	void Rewrite(const std::filesystem::path& path, const std::string& contents)
	{
		const std::filesystem::file_time_type before = std::filesystem::last_write_time(path);

		{
			std::ofstream stream{path, std::ios::binary | std::ios::trunc};
			stream << contents;
		}

		std::filesystem::last_write_time(path, before + std::chrono::seconds(60));
	}

	bool HasToastSaying(const std::vector<Graphite::ToastEvent>& toasts, const std::string& message)
	{
		return std::ranges::any_of(toasts, [&message](const Graphite::ToastEvent& toast) { return toast.Message == message; });
	}
}

TEST_CASE("A binary file opens as a hex tab with its path row and rows")
{
	TempDirectory folder;
	const std::filesystem::path program = folder.MakeFile("program.exe", ProgramBytes(32));

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(program, true);
	drawn.ShowLanded();

	REQUIRE(drawn.Documents.GetCount() == 1);
	REQUIRE(drawn.Documents.At(0).View == Core::ItemView::Hex);

	CHECK(drawn.TabDraws(program, "program.exe", Face::Interface));
	CHECK(drawn.TabDraws(program, "Offset", Face::Code));
	CHECK(drawn.TabDraws(program, "00000000", Face::Code));
	CHECK(drawn.TabDraws(program, "4D 5A 90 00", Face::Code));
	CHECK(drawn.TabDraws(program, "00000010", Face::Code));
	CHECK_FALSE(drawn.TabDraws(program, "00000020", Face::Code));
}

TEST_CASE("A hex tab gives the status bar its offset, count, values and size")
{
	TempDirectory folder;
	const std::filesystem::path program = folder.MakeFile("program.exe", ProgramBytes(32));
	const std::filesystem::path notes = folder.MakeFile("notes.txt", "some notes\n");

	DrawnDocuments drawn(folder.GetPath());
	drawn.Places[notes] = kBesideTheFirst;
	drawn.Windows.RequestOpen(notes, true);
	drawn.Windows.RequestOpen(program, true);
	drawn.ShowLanded();

	using Graphite::ViewStatusItem;

	CHECK(drawn.StatusItems() == std::vector<ViewStatusItem>{{"Offset 0x00000000", false, std::nullopt},
															 {"1 byte", false, std::nullopt},
															 {"u8 77  u16 23117  u32 9460301", true, std::nullopt},
															 {"32 B", false, std::nullopt}});

	drawn.Focus(program);
	drawn.ShiftRight(2);

	CHECK(drawn.StatusItems() == std::vector<ViewStatusItem>{{"Offset 0x00000000", false, std::nullopt},
															 {"3 bytes", false, std::nullopt},
															 {"u8 77  u16 23117  u32 9460301", true, std::nullopt},
															 {"32 B", false, std::nullopt}});

	drawn.Focus(notes);

	CHECK(drawn.StatusItems().empty());
}

TEST_CASE("Ctrl+C copies the selected bytes as hex pairs")
{
	TempDirectory folder;
	const std::filesystem::path program = folder.MakeFile("program.exe", ProgramBytes(32));

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(program, true);
	drawn.ShowLanded();
	drawn.Focus(program);
	drawn.ShiftRight(3);

	CHECK(drawn.Windows.CanRun(Graphite::EditorAction::Copy));
	CHECK(drawn.Windows.CanRun(Graphite::EditorAction::SelectAll));
	CHECK_FALSE(drawn.Windows.CanRun(Graphite::EditorAction::Cut));
	CHECK_FALSE(drawn.Windows.CanRun(Graphite::EditorAction::Paste));
	CHECK_FALSE(drawn.Windows.CanRun(Graphite::EditorAction::Undo));
	CHECK_FALSE(drawn.Windows.CanRun(Graphite::EditorAction::Redo));

	drawn.Press(Core::KeyCode::C, Core::KeyModifier::Control);

	CHECK(drawn.Clipboard == "4D 5A 90 00");
	CHECK(drawn.Toasts.empty());

	SUBCASE("Ctrl+A then Ctrl+C copies every byte")
	{
		drawn.Press(Core::KeyCode::A, Core::KeyModifier::Control);
		drawn.Press(Core::KeyCode::C, Core::KeyModifier::Control);

		REQUIRE(drawn.StatusItems().size() > 1);
		CHECK(drawn.StatusItems()[1].Text == "32 bytes");
		CHECK(drawn.Clipboard.starts_with("4D 5A 90 00 03 00 00 00 49 4A"));
		CHECK(drawn.Clipboard.ends_with("5A 41 42 43 44 45 46"));
		CHECK(drawn.Clipboard.size() == 32 * 3 - 1);
	}
}

TEST_CASE("Ctrl+A then Ctrl+C on a large file is refused with a toast and reads nothing")
{
	TempDirectory folder;
	std::filesystem::path large;
	std::string count;

	SUBCASE("One byte past 1 MiB")
	{
		large = folder.MakeFile("large.bin", std::string(1024 * 1024 + 1, '\0'));
		count = "1048577 bytes";
	}

	SUBCASE("5 GiB")
	{
		large = folder.GetPath() / "huge.bin";
		count = "5368709120 bytes";

		if (!TryWriteSparseFile(large, 5ull * 1024 * 1024 * 1024))
		{
			MESSAGE("Skipped: the temporary directory's filesystem cannot make a file sparse");
			return;
		}
	}

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(large, true);
	drawn.ShowLanded();
	drawn.Focus(large);

	REQUIRE(drawn.Jobs.Count() == 0);

	drawn.Press(Core::KeyCode::A, Core::KeyModifier::Control);
	REQUIRE(drawn.StatusItems().size() > 1);
	CHECK(drawn.StatusItems()[1].Text == count);

	drawn.Press(Core::KeyCode::C, Core::KeyModifier::Control);

	CHECK(drawn.Clipboard == kUntouchedClipboard);
	CHECK(HasToastSaying(drawn.Toasts, "Select at most 1 MB to copy"));
	CHECK(drawn.Jobs.Count() == 0);
}

TEST_CASE("Copy from the menu reports the copy")
{
	TempDirectory folder;
	const std::filesystem::path program = folder.MakeFile("program.exe", ProgramBytes(32));

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(program, true);
	drawn.ShowLanded();
	drawn.Focus(program);
	drawn.ShiftRight(3);

	CHECK(drawn.RunFromMenu(Graphite::kCopyCommand));

	CHECK(drawn.Clipboard == "4D 5A 90 00");
	REQUIRE(drawn.Toasts.size() == 1);
	CHECK(drawn.Toasts.front().Kind == Core::ToastKind::Success);
	CHECK(drawn.Toasts.front().Title == "Copied");
	CHECK(drawn.Toasts.front().Message == "bytes: 4D 5A 90 00");
}

TEST_CASE("A file that grows while its hex tab is open shows its new rows")
{
	TempDirectory folder;
	const std::filesystem::path log = folder.MakeFile("growing.bin", ProgramBytes(32));

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(log, true);
	drawn.ShowLanded();

	REQUIRE(drawn.TabDraws(log, "00000010", Face::Code));
	REQUIRE_FALSE(drawn.TabDraws(log, "00000020", Face::Code));

	Rewrite(log, ProgramBytes(48));

	SUBCASE("The workspace reports the change")
	{
		drawn.Windows.HandleFilesChanged({folder.GetPath()}, false);
	}

	SUBCASE("The application comes back to the front")
	{
		drawn.Windows.NotifyWindowFocus(false);
		drawn.Windows.NotifyWindowFocus(true);
	}

	drawn.Jobs.RunAll();
	drawn.Frame();
	drawn.Frame();

	CHECK(drawn.TabDraws(log, "00000020", Face::Code));
	CHECK(drawn.TabDraws(log, "47 48 49 4A", Face::Code));
	REQUIRE_FALSE(drawn.StatusItems().empty());
	CHECK(drawn.StatusItems().back().Text == "48 B");
}

TEST_CASE("Ctrl+W closes a focused hex tab")
{
	TempDirectory folder;
	const std::filesystem::path program = folder.MakeFile("program.exe", ProgramBytes(32));
	const std::filesystem::path notes = folder.MakeFile("notes.txt", "some notes\n");

	DrawnDocuments drawn(folder.GetPath());
	drawn.Places[notes] = kBesideTheFirst;
	drawn.Windows.RequestOpen(notes, true);
	drawn.Windows.RequestOpen(program, true);
	drawn.ShowLanded();
	drawn.Focus(program);

	REQUIRE(drawn.Documents.GetCount() == 2);

	drawn.Press(Core::KeyCode::W, Core::KeyModifier::Control);

	CHECK_FALSE(drawn.Documents.Find(program).has_value());
	CHECK(drawn.Documents.Find(notes).has_value());
	CHECK(drawn.StatusItems().empty());
}

TEST_CASE("Arrow keys in a hex tab do not reach a text tab")
{
	TempDirectory folder;
	const std::filesystem::path program = folder.MakeFile("program.exe", ProgramBytes(32));
	const std::filesystem::path notes = folder.MakeFile("notes.txt", "first line\nsecond line\n");

	DrawnDocuments drawn(folder.GetPath());
	drawn.Places[notes] = kBesideTheFirst;
	drawn.Windows.RequestOpen(notes, true);
	drawn.Windows.RequestOpen(program, true);
	drawn.ShowLanded();

	const std::optional<std::size_t> notesIndex = drawn.Documents.Find(notes);
	REQUIRE(notesIndex.has_value());
	const std::size_t secondLine = std::string("first line\n").size();

	// The text tab takes the arrows while its text holds the keyboard, so they have somewhere else to go.
	drawn.FocusText(notes);
	drawn.Press(Core::KeyCode::Down, Core::KeyModifier::None);
	REQUIRE(drawn.Documents.At(*notesIndex).Caret.Offset == secondLine);

	drawn.Focus(program);
	drawn.Press(Core::KeyCode::Up, Core::KeyModifier::None);
	drawn.Press(Core::KeyCode::Down, Core::KeyModifier::None);
	drawn.Press(Core::KeyCode::Right, Core::KeyModifier::None);

	REQUIRE_FALSE(drawn.StatusItems().empty());
	CHECK(drawn.StatusItems().front().Text == "Offset 0x00000011");
	CHECK(drawn.Documents.At(*notesIndex).Caret.Offset == secondLine);
}

namespace {

	constexpr float kEditorFontSize = 16.0f;
	const ImVec2 kHeldReadWindowSize{900.0f, 600.0f};

	struct DocumentWindowsWithHeldReads
	{
		ImGuiFrames Frames;
		DeferredJobs Jobs;
		Core::DocumentSet Documents;
		Graphite::ModalGate Modals;
		SilentServer Server;
		Graphite::DiagnosticsModel Diagnostics;
		NoBases Bases;
		Graphite::ChangeMarkersModel ChangeMarkers{Bases, [](std::function<void()>) {}};
		SilentPreviewPort PreviewServer;
		Graphite::PreviewSemantics PreviewSemantics{PreviewServer};
		Graphite::AnalysisCache Analysis{kAnalysisBudgetBytes};
		Graphite::AnalysisStore AnalysisStore;
		SilentAnalysisPort AnalysisServer;
		SilentTexts Texts;
		Graphite::WorkspaceAnalysis WorkspaceAnalysis{AnalysisServer, Texts, Analysis, nullptr};
		Core::EventBus Events;
		Graphite::ActiveEditorTarget ActiveEditor;
		FakeTextureFactory Textures;
		Graphite::ImageTabs Images{Jobs.Submitter(), Textures};
		Graphite::HexFiles Hex{Jobs.Submitter()};
		Graphite::DocumentWindows Windows{Documents, Modals, Server, Diagnostics, ChangeMarkers, PreviewSemantics,
										  Analysis, AnalysisStore, WorkspaceAnalysis, Events, ActiveEditor, Images, Hex};

		TempAppSettings Settings;
		Graphite::Workspace Workspace{Events, Settings.Get()};
		Graphite::ActiveScheme Scheme;
		Graphite::EditorSettings Editor{Settings.Get()};
		Graphite::ReferencesPanel References{Workspace, Documents, Scheme, Editor, PreviewSemantics};
		Core::TabCloseRequests CloseRequests;
		std::filesystem::path NoRoot;
		std::map<std::filesystem::path, ImVec2> WindowSizes;

		DocumentWindowsWithHeldReads()
		{
			Documents.LoadLargeFilesWith(Jobs.Submitter());
		}

		void FinishReads()
		{
			Jobs.RunAll();
			Windows.LandFinishedLoads();
		}

		void DrawFrame(bool wordWrap)
		{
			Frames.Next([this, wordWrap] {
				for (std::size_t index = 0; index < Documents.GetCount(); ++index)
				{
					const std::string windowName = Graphite::DocumentWindowName(Documents, index);

					const std::filesystem::path& path = Documents.At(index).Path;
					const ImVec2 size = WindowSizes.contains(path) ? WindowSizes.at(path) : kHeldReadWindowSize;

					if (ImGui::FindWindowByName(windowName.c_str()) != nullptr)
					{
						ImGui::SetWindowSize(windowName.c_str(), size);
					}
				}

				const Graphite::CommandContext noCommands;
				Windows.Render(noCommands,
							   Graphite::EditorFrame{.FontSize = kEditorFontSize,
													 .Indent = Core::IndentStyle{},
													 .FallbackDockNode = 0,
													 .WordWrap = wordWrap,
													 .ChangeMarkers = Graphite::ChangeMarkerDisplay{},
													 .HighlightOccurrences = false,
													 .Hover = Graphite::HoverSettings{},
													 .Scheme = Graphite::DarkScheme(),
													 .CloseRequests = CloseRequests,
													 .WorkspaceRoot = NoRoot,
													 .Settings = Editor},
							   References);
			});
		}
	};

	// WARNING: A finished read reaches the set through its mailbox, which asks for a frame; nothing else in a read does.
	bool RunningThemPostsAResult(DeferredJobs& jobs)
	{
		Core::Redraw::TakeRequest();
		jobs.RunAll();

		return Core::Redraw::IsRequested();
	}

	std::string NumberedLinesPastBackgroundThreshold()
	{
		std::string text;
		std::size_t lineNumber = 0;

		while (text.size() <= Core::BackgroundLoadThreshold)
		{
			text += "line " + std::to_string(lineNumber) + " of a large log\n";
			++lineNumber;
		}

		return text;
	}

	const Graphite::TabState* TabFor(const Graphite::WorkspaceState& state, const std::filesystem::path& path)
	{
		for (const Graphite::TabState& tab : state.Tabs)
		{
			if (tab.Path == path)
			{
				return &tab;
			}
		}

		return nullptr;
	}
}

namespace {

	// The image tab's load fails, then the hex view of the same file asks for its first rows.
	void ShowFailedImage(DrawnDocuments& drawn)
	{
		drawn.ShowLanded();
		drawn.Jobs.RunAll();
		drawn.Frame();
		drawn.Frame();
	}
}

TEST_CASE("An image that cannot be decoded is shown as hex under the reason")
{
	TempDirectory folder;
	// A NUL makes it binary, which is what sends a file named .png to an image tab rather than a text one.
	const std::filesystem::path broken = folder.MakeFile("broken.png", std::string("this\0is not a picture", 21));
	const std::filesystem::path notes = folder.MakeFile("notes.txt", "some notes\n");

	DrawnDocuments drawn(folder.GetPath());
	drawn.Places[notes] = kBesideTheFirst;
	drawn.Windows.RequestOpen(notes, true);
	drawn.Windows.RequestOpen(broken, true);
	ShowFailedImage(drawn);

	REQUIRE(drawn.Documents.At(*drawn.Documents.Find(broken)).View == Core::ItemView::Image);
	REQUIRE(drawn.Picture(broken).GetState() == Graphite::ImageTabState::Failed);

	const std::string notice = drawn.Picture(broken).GetFailure();
	REQUIRE(notice.starts_with("Could not read this image: "));

	CHECK(drawn.TabDraws(broken, "broken.png", Face::Interface));
	CHECK(drawn.TabDraws(broken, notice, Face::Interface));
	CHECK(drawn.TabDraws(broken, "Offset", Face::Code));
	CHECK(drawn.TabDraws(broken, "74 68 69 73", Face::Code));
	CHECK_FALSE(drawn.TabDraws(broken, "Retry", Face::Interface));

	using Graphite::ViewStatusItem;

	CHECK(drawn.StatusItems() == std::vector<ViewStatusItem>{{"Offset 0x00000000", false, std::nullopt},
															 {"1 byte", false, std::nullopt},
															 {"u8 116  u16 26740  u32 1936287860", true, std::nullopt},
															 {"21 B", false, std::nullopt}});

	SUBCASE("The zoom chords and the menu zoom go to the font")
	{
		drawn.Focus(broken);
		const float font = drawn.Settings.GetFontSize();

		drawn.Press(Core::KeyCode::Equal, Core::KeyModifier::Control);
		CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font + 1.0f));

		CHECK(drawn.RunFromMenu("editor.zoomIn"));
		CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font + 2.0f));
	}

	SUBCASE("Copy and Select All work on its bytes")
	{
		drawn.Focus(broken);

		CHECK(drawn.Windows.CanRun(Graphite::EditorAction::Copy));
		CHECK(drawn.Windows.CanRun(Graphite::EditorAction::SelectAll));

		drawn.ShiftRight(3);
		drawn.Press(Core::KeyCode::C, Core::KeyModifier::Control);
		CHECK(drawn.Clipboard == "74 68 69 73");

		drawn.Press(Core::KeyCode::A, Core::KeyModifier::Control);
		REQUIRE(drawn.StatusItems().size() > 1);
		CHECK(drawn.StatusItems()[1].Text == "21 bytes");
	}

	SUBCASE("The file mended on disk comes back as a picture")
	{
		Rewrite(broken, Contents(ImageFixtures::kPng2x2));
		drawn.Windows.HandleFilesChanged({folder.GetPath()}, false);
		REQUIRE(drawn.Jobs.Count() > 0);
		drawn.Jobs.RunAll();
		drawn.Frame();
		drawn.Frame();
		drawn.Focus(broken);

		CHECK(drawn.Picture(broken).GetState() == Graphite::ImageTabState::Ready);
		CHECK(drawn.StatusItems() == std::vector<ViewStatusItem>{{"2 × 2 · PNG · 77 B", false, std::nullopt}});
		CHECK_FALSE(drawn.TabDraws(broken, "Offset", Face::Code));

		const float font = drawn.Settings.GetFontSize();
		drawn.Press(Core::KeyCode::Equal, Core::KeyModifier::Control);
		CHECK(drawn.Picture(broken).GetZoom().Zoom == 1.5f);
		CHECK(drawn.Settings.GetFontSize() == doctest::Approx(font));
		CHECK_FALSE(drawn.Windows.CanRun(Graphite::EditorAction::Copy));
	}
}

TEST_CASE("An image past the pixel limit is shown as hex under its size")
{
	TempDirectory folder;
	const std::filesystem::path huge = folder.MakeFile("huge.png", Contents(ImageFixtures::kPngHeader20000x12000));

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(huge, true);
	ShowFailedImage(drawn);

	REQUIRE(drawn.Picture(huge).GetState() == Graphite::ImageTabState::Failed);

	CHECK(drawn.TabDraws(huge, "Image is too large to preview (20000 × 12000)", Face::Interface));
	CHECK(drawn.TabDraws(huge, "89 50 4E 47", Face::Code));
	CHECK_FALSE(drawn.TabDraws(huge, "Retry", Face::Interface));
	REQUIRE_FALSE(drawn.StatusItems().empty());
	CHECK(drawn.StatusItems().front().Text == "Offset 0x00000000");
}

TEST_CASE("An image that cannot be read keeps its failure with Retry")
{
	TempDirectory folder;
	const std::filesystem::path picture = folder.MakeFile("logo.png", Contents(ImageFixtures::kPng2x2));

	DrawnDocuments drawn(folder.GetPath());
	drawn.Windows.RequestOpen(picture, true);
	drawn.Frame();
	std::filesystem::remove(picture);
	ShowFailedImage(drawn);

	REQUIRE(drawn.Picture(picture).GetState() == Graphite::ImageTabState::Failed);
	REQUIRE(drawn.Picture(picture).GetFailureKind() == Graphite::ImageFailure::Read);

	CHECK(drawn.TabDraws(picture, drawn.Picture(picture).GetFailure(), Face::Interface));
	CHECK(drawn.TabDraws(picture, "Retry", Face::Interface));
	CHECK_FALSE(drawn.TabDraws(picture, "Offset", Face::Code));
	CHECK(drawn.StatusItems().empty());
	CHECK_FALSE(drawn.Windows.CanRun(Graphite::EditorAction::Copy));
}

TEST_CASE("A tab whose large file is still being read is remembered as open and active")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RequestOpen(path, true);
	REQUIRE(Core::IsLoading(fixture.Documents.At(0)));

	const Graphite::WorkspaceState state = fixture.Windows.CaptureState();

	const Graphite::TabState* tab = TabFor(state, path);
	REQUIRE(tab != nullptr);
	CHECK_FALSE(tab->Preview);
	CHECK(tab->Line == 0);
	CHECK(tab->Column == 0);
	CHECK(state.Active == path);
}

TEST_CASE("A restored tab whose large file is still being read is remembered where it was")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	Graphite::WorkspaceState restored;
	restored.Tabs.push_back(Graphite::TabState{.Path = path, .Preview = false, .Line = 1200, .Column = 4, .Top = 1180});
	restored.Active = path;

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RestoreState(restored);
	REQUIRE(Core::IsLoading(fixture.Documents.At(0)));

	const Graphite::WorkspaceState state = fixture.Windows.CaptureState();

	const Graphite::TabState* tab = TabFor(state, path);
	REQUIRE(tab != nullptr);
	CHECK(*tab == restored.Tabs.front());
	CHECK(state.Active == path);
}

TEST_CASE("A restored tab whose large file is read on a worker gets its caret and top line once the text lands")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	Graphite::WorkspaceState restored;
	restored.Tabs.push_back(Graphite::TabState{.Path = path, .Preview = false, .Line = 1200, .Column = 4, .Top = 1180});

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RestoreState(restored);
	fixture.FinishReads();

	const Core::OpenItem& item = fixture.Documents.At(0);
	REQUIRE(item.Document != nullptr);
	CHECK(item.Caret.Offset == Graphite::RestoredCaretOffset(item.Document->GetBuffer(), 1200, 4));

	const Graphite::WorkspaceState state = fixture.Windows.CaptureState();

	const Graphite::TabState* tab = TabFor(state, path);
	REQUIRE(tab != nullptr);
	CHECK(tab->Line == 1200);
	CHECK(tab->Column == 4);
	CHECK(tab->Top == 1180);
}

TEST_CASE("A tab whose large file comes back reads it on a worker rather than where the change was noticed")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.GetPath() / "appears.log";

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RequestOpen(path, true);
	REQUIRE(fixture.Documents.At(0).Failure == Core::OpenFailure::Missing);

	WriteFileBytes(path, NumberedLinesPastBackgroundThreshold());

	const bool everythingChanged = true;
	fixture.Windows.HandleFilesChanged({}, everythingChanged);

	CHECK(Core::IsLoading(fixture.Documents.At(0)));

	fixture.FinishReads();
	CHECK(fixture.Documents.At(0).Document != nullptr);
}

TEST_CASE("Closing a tab whose file is still being read closes it and gives up the read")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RequestOpen(path, true);
	REQUIRE(Core::IsLoading(fixture.Documents.At(0)));

	const bool wordWrap = false;
	fixture.DrawFrame(wordWrap);

	CHECK_FALSE(fixture.Windows.CanSave());
	REQUIRE(fixture.Windows.CanClose());
	fixture.Windows.Close();
	fixture.DrawFrame(wordWrap);

	CHECK(fixture.Documents.GetCount() == 0);
	CHECK_FALSE(RunningThemPostsAResult(fixture.Jobs));
}

TEST_CASE("A file changed on disk while it was read is reloaded once the read lands")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RequestOpen(path, true);
	fixture.Jobs.RunAll();

	const std::string changedText = NumberedLinesPastBackgroundThreshold() + "one line more\n";
	WriteFileBytes(path, changedText);
	fixture.Windows.LandFinishedLoads();

	const Core::OpenItem& item = fixture.Documents.At(0);
	REQUIRE(item.Document != nullptr);
	CHECK(item.Document->GetBuffer().GetLength() == changedText.size());
	CHECK_FALSE(item.Document->CheckChangedOnDisk());
}

TEST_CASE("A tab whose file is still being read saves the selection asked for while it loads")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	const Graphite::OpenDocument openPermanently = [&fixture](const std::filesystem::path& file) { fixture.Windows.RequestOpen(file, true); };

	REQUIRE(Graphite::SelectInDocument(fixture.Documents, path, 3, 2, 5, openPermanently));
	REQUIRE(Core::IsLoading(fixture.Documents.At(0)));

	const Graphite::WorkspaceState state = fixture.Windows.CaptureState();

	const Graphite::TabState* tab = TabFor(state, path);
	REQUIRE(tab != nullptr);
	CHECK(tab->Line == 3);
	CHECK(tab->Column == 5);

	fixture.FinishReads();

	const Core::OpenItem& item = fixture.Documents.At(0);
	REQUIRE(item.Document != nullptr);
	const std::size_t lineStart = item.Document->GetBuffer().LineStart(3);
	CHECK(item.Caret.Anchor == lineStart + 2);
	CHECK(item.Caret.Offset == lineStart + 5);
}

TEST_CASE("A UTF-16 caret asked for while a file loads is saved in its unit and restored on the same character")
{
	TempDirectory folder;
	const std::string firstLine = "\xc4\x85\xc4\x85 target";
	const std::filesystem::path path = folder.MakeFile("big.log", firstLine + "\n" + NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	const Graphite::OpenDocument openPermanently = [&fixture](const std::filesystem::path& file) { fixture.Windows.RequestOpen(file, true); };

	constexpr std::size_t kUtf16ColumnOfTarget = 3;
	constexpr std::size_t kByteOfTarget = 5;
	REQUIRE(Graphite::SelectInDocument(fixture.Documents, path, 0, Graphite::Utf16Column(kUtf16ColumnOfTarget), 0,
									   Graphite::Utf16Column(kUtf16ColumnOfTarget), openPermanently));

	const Graphite::WorkspaceState saved = fixture.Windows.CaptureState();
	const Graphite::TabState* tab = TabFor(saved, path);
	REQUIRE(tab != nullptr);
	CHECK(tab->Column == kUtf16ColumnOfTarget);
	CHECK(tab->ColumnIn == Core::ColumnUnit::Utf16CodeUnit);

	fixture.Documents.CloseAll();
	fixture.Windows.RestoreState(saved);
	fixture.FinishReads();

	const Core::OpenItem& item = fixture.Documents.At(0);
	REQUIRE(item.Document != nullptr);
	CHECK(item.Caret.Offset == kByteOfTarget);
}

TEST_CASE("A large file read in a narrow pane while a wide pane draws last is wrapped for its own pane")
{
	TempDirectory folder;
	const std::filesystem::path big = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());
	const std::filesystem::path small = folder.MakeFile("small.txt", "a short file\n");

	DocumentWindowsWithHeldReads fixture;
	fixture.WindowSizes[big] = ImVec2(500.0f, 600.0f);
	fixture.WindowSizes[small] = ImVec2(1100.0f, 600.0f);

	const bool wordWrap = true;
	fixture.Windows.RequestOpen(big, true);
	fixture.Windows.RequestOpen(small, true);
	REQUIRE(Core::IsLoading(fixture.Documents.At(0)));

	fixture.DrawFrame(wordWrap);
	fixture.DrawFrame(wordWrap);
	fixture.DrawFrame(wordWrap);
	fixture.FinishReads();

	const Core::OpenItem& item = fixture.Documents.At(0);
	REQUIRE(item.Document != nullptr);
	const std::size_t columnsBuiltOnTheWorker = item.ColumnsPerWrappedRow;
	REQUIRE(columnsBuiltOnTheWorker != Core::RowMap::kNoWrap);

	fixture.DrawFrame(wordWrap);
	CHECK(item.ColumnsPerWrappedRow == columnsBuiltOnTheWorker);
}

TEST_CASE("A large file opened with word wrap on is wrapped on the worker as its pane draws it")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	const bool wordWrap = true;

	fixture.Windows.RequestOpen(path, true);
	fixture.DrawFrame(wordWrap);
	fixture.DrawFrame(wordWrap);
	fixture.FinishReads();

	const Core::OpenItem& item = fixture.Documents.At(0);
	REQUIRE(item.Document != nullptr);
	const std::size_t columnsBuiltOnTheWorker = item.ColumnsPerWrappedRow;
	REQUIRE(columnsBuiltOnTheWorker != Core::RowMap::kNoWrap);

	fixture.DrawFrame(wordWrap);
	CHECK(item.ColumnsPerWrappedRow == columnsBuiltOnTheWorker);

	fixture.DrawFrame(wordWrap);
	CHECK(item.ColumnsPerWrappedRow == columnsBuiltOnTheWorker);
}
