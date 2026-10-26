#include <doctest/doctest.h>

#include "Core/Image/RgbaImage.h"
#include "Core/ImGui/ImageTexture.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <vector>

namespace {

	constexpr ImTextureID kFakeTextureId = 42;
	constexpr ImTextureID kUploadedTextureId = 7;
	constexpr int kMaximumSide = 4096;

	// A context of its own with no renderer backend behind it, run frame by frame.
	class HeadlessFrames
	{
	public:
		HeadlessFrames()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.Fonts->Build();
		}

		~HeadlessFrames() { ImGui::DestroyContext(m_Context); }

		HeadlessFrames(const HeadlessFrames&) = delete;
		HeadlessFrames& operator=(const HeadlessFrames&) = delete;

		// ImGui lists the user textures in the platform IO as a frame ends.
		void Next()
		{
			ImGui::NewFrame();
			ImGui::EndFrame();
		}

		// The commands of the window the texture was drawn into, as the draw left them.
		std::vector<ImDrawCmd> Draw(const Core::ImageTexture& texture, Core::TextureSampling sampling)
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
			ImGui::Begin("Picture", nullptr,
						 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
							 ImGuiWindowFlags_NoSavedSettings);

			ImDrawList& drawList = *ImGui::GetWindowDrawList();
			Core::DrawTexture(drawList, texture, ImRect(10.0f, 10.0f, 74.0f, 42.0f), sampling);
			const std::vector<ImDrawCmd> commands(drawList.CmdBuffer.begin(), drawList.CmdBuffer.end());

			ImGui::End();
			ImGui::EndFrame();

			return commands;
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};

	class FakeTexture final : public Core::ImageTexture
	{
	public:
		ImTextureRef GetRef() const override { return ImTextureRef(kFakeTextureId); }

		ImVec2 GetSize() const override { return ImVec2(64.0f, 32.0f); }
	};

	// WARNING: the two bodies differ so the linker cannot fold the callbacks into one address.
	int g_nearestRequests = 0;
	int g_linearRequests = 0;

	void SetSamplerNearest(const ImDrawList*, const ImDrawCmd*)
	{
		++g_nearestRequests;
	}

	void SetSamplerLinear(const ImDrawList*, const ImDrawCmd*)
	{
		++g_linearRequests;
	}

	// As the OpenGL3 backend fills them when it starts.
	void InstallSamplerCallbacks()
	{
		ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
		platformIo.DrawCallback_SetSamplerNearest = &SetSamplerNearest;
		platformIo.DrawCallback_SetSamplerLinear = &SetSamplerLinear;
	}

	bool DrawsFakeTexture(const ImDrawCmd& command)
	{
		return command.UserCallback == nullptr && command.ElemCount > 0 && command.TexRef == ImTextureRef(kFakeTextureId);
	}

	bool HasCallback(const ImDrawCmd& command)
	{
		return command.UserCallback != nullptr;
	}

	bool Calls(const ImDrawCmd& command, ImDrawCallback callback)
	{
		return command.UserCallback == callback;
	}

	Core::RgbaImage TwoByOne()
	{
		return Core::RgbaImage{.Width = 2, .Height = 1, .Pixels = {255, 0, 0, 255, 0, 0, 255, 128}};
	}

	int ListedTextures()
	{
		return ImGui::GetPlatformIO().Textures.Size;
	}
}

TEST_CASE("Drawing a texture with nearest sampling brackets it with the backend's sampler callbacks")
{
	HeadlessFrames frames;
	InstallSamplerCallbacks();

	const std::vector<ImDrawCmd> commands = frames.Draw(FakeTexture{}, Core::TextureSampling::Nearest);

	const auto nearest = std::ranges::find_if(
		commands, [](const ImDrawCmd& command) { return Calls(command, &SetSamplerNearest); });

	REQUIRE(nearest != commands.end());
	REQUIRE(std::distance(nearest, commands.end()) >= 3);
	CHECK(DrawsFakeTexture(*std::next(nearest)));
	CHECK(Calls(*std::next(nearest, 2), &SetSamplerLinear));
	CHECK(std::ranges::count_if(commands, HasCallback) == 2);
}

TEST_CASE("Drawing a texture smoothly adds no sampler callback")
{
	HeadlessFrames frames;
	InstallSamplerCallbacks();

	const std::vector<ImDrawCmd> commands = frames.Draw(FakeTexture{}, Core::TextureSampling::Smooth);

	CHECK(std::ranges::count_if(commands, DrawsFakeTexture) == 1);
	CHECK(std::ranges::none_of(commands, HasCallback));
}

TEST_CASE("Without the backend's callbacks a nearest draw still draws the texture")
{
	HeadlessFrames frames;
	const ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
	REQUIRE((platformIo.DrawCallback_SetSamplerNearest == nullptr));
	REQUIRE((platformIo.DrawCallback_SetSamplerLinear == nullptr));

	const std::vector<ImDrawCmd> commands = frames.Draw(FakeTexture{}, Core::TextureSampling::Nearest);

	CHECK(std::ranges::count_if(commands, DrawsFakeTexture) == 1);
	CHECK(std::ranges::none_of(commands, HasCallback));
}

TEST_CASE("The ImGui factory copies the picture into an RGBA texture of its size")
{
	HeadlessFrames frames;
	Core::ImGuiTextureFactory factory(kMaximumSide);
	const Core::RgbaImage image = TwoByOne();

	const std::unique_ptr<Core::ImageTexture> texture = factory.Create(image);

	REQUIRE(texture->GetRef()._TexData != nullptr);
	const ImTextureData& data = *texture->GetRef()._TexData;
	CHECK(data.Format == ImTextureFormat_RGBA32);
	CHECK(data.Width == 2);
	CHECK(data.Height == 1);
	CHECK(data.Status == ImTextureStatus_WantCreate);
	CHECK(std::vector<std::uint8_t>(data.Pixels, data.Pixels + data.GetSizeInBytes()) == image.Pixels);
	CHECK(texture->GetSize().x == 2.0f);
	CHECK(texture->GetSize().y == 1.0f);
	CHECK(factory.GetMaximumSide() == kMaximumSide);
}

// The test plays the backend: a headless context has none to upload or destroy the texture.
TEST_CASE("A texture from the ImGui factory is registered until it is released")
{
	HeadlessFrames frames;
	Core::ImGuiTextureFactory factory(kMaximumSide);
	frames.Next();
	const int listedBefore = ListedTextures();

	std::unique_ptr<Core::ImageTexture> texture = factory.Create(TwoByOne());
	ImTextureData* data = texture->GetRef()._TexData;
	REQUIRE(data != nullptr);

	frames.Next();
	CHECK(ListedTextures() == listedBefore + 1);
	CHECK(ImGui::GetPlatformIO().Textures.contains(data));

	data->SetTexID(kUploadedTextureId);
	data->SetStatus(ImTextureStatus_OK);

	texture.reset();
	frames.Next();
	CHECK(data->Status == ImTextureStatus_WantDestroy);

	factory.CollectRetired();
	frames.Next();
	CHECK(ImGui::GetPlatformIO().Textures.contains(data));

	data->SetTexID(ImTextureID_Invalid);
	data->SetStatus(ImTextureStatus_Destroyed);
	REQUIRE(data->Status == ImTextureStatus_Destroyed);

	factory.CollectRetired();
	CHECK(ListedTextures() == listedBefore);

	frames.Next();
	CHECK(ListedTextures() == listedBefore);
}

TEST_CASE("A texture released before the backend saw it is kept through its frame and freed after the next begins")
{
	HeadlessFrames frames;
	Core::ImGuiTextureFactory factory(kMaximumSide);
	frames.Next();
	const int listedBefore = ListedTextures();

	std::unique_ptr<Core::ImageTexture> texture = factory.Create(TwoByOne());
	const ImTextureData* data = texture->GetRef()._TexData;
	REQUIRE(data != nullptr);

	texture.reset();

	// The frame being built may still draw it, so the backend has to upload it all the same.
	CHECK(data->Status == ImTextureStatus_WantCreate);

	factory.CollectRetired();
	frames.Next();
	CHECK(ListedTextures() == listedBefore + 1);
	CHECK(data->Status == ImTextureStatus_Destroyed);

	factory.CollectRetired();
	CHECK(ListedTextures() == listedBefore);
}
