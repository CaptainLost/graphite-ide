#include "corepch.h"

#include "Core/ImGui/ImageTexture.h"

#include "Core/Base/Assert.h"
#include "Core/Image/RgbaImage.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

namespace Core {

	namespace {
		// WARNING: ImGui rebuilds the platform's texture list only as a frame ends, so until then it would still point
		// at a freed texture, and the backend reads that list when it shuts down.
		void Unregister(ImTextureData& data)
		{
			ImGui::UnregisterUserTexture(&data);
			ImGui::GetPlatformIO().Textures.find_erase(&data);
		}
	}

	class ImGuiTextureFactory::Texture final : public ImageTexture
	{
	public:
		Texture(ImGuiTextureFactory& factory, std::unique_ptr<ImTextureData> data)
			: m_Factory(factory), m_Data(std::move(data))
		{
			++m_Factory.m_LiveTextures;
		}

		// WARNING: the frame being built may still draw this texture, so it is only marked for destruction. ImGui
		// turns the mark into WantDestroy as the next frame begins, and without it the backend's Destroyed would be
		// taken as a texture lost and to be created again.
		~Texture() override
		{
			m_Data->WantDestroyNextFrame = true;
			m_Factory.m_Retired.push_back(std::move(m_Data));
			--m_Factory.m_LiveTextures;
		}

		ImTextureRef GetRef() const override { return m_Data->GetTexRef(); }

		ImVec2 GetSize() const override
		{
			return ImVec2(static_cast<float>(m_Data->Width), static_cast<float>(m_Data->Height));
		}

	private:
		ImGuiTextureFactory& m_Factory;
		std::unique_ptr<ImTextureData> m_Data;
	};

	ImGuiTextureFactory::ImGuiTextureFactory(int maximumSide) : m_MaximumSide(maximumSide) {}

	ImGuiTextureFactory::~ImGuiTextureFactory()
	{
		CORE_ASSERT(m_LiveTextures == 0, "A texture outlived the factory that made it");

		for (const std::unique_ptr<ImTextureData>& data : m_Retired)
		{
			Unregister(*data);
		}
	}

	std::unique_ptr<ImageTexture> ImGuiTextureFactory::Create(const RgbaImage& image)
	{
		CORE_ASSERT(image.Width > 0 && image.Height > 0, "A texture needs at least one pixel");
		CORE_ASSERT(image.Width <= m_MaximumSide && image.Height <= m_MaximumSide, "The image is past the texture limit");

		auto data = std::make_unique<ImTextureData>();
		data->Create(ImTextureFormat_RGBA32, image.Width, image.Height);

		const std::size_t textureBytes = static_cast<std::size_t>(data->GetSizeInBytes());
		CORE_ASSERT(image.ByteSize() == textureBytes, "The image's pixels do not match its size");
		std::memcpy(data->Pixels, image.Pixels.data(), std::min(image.ByteSize(), textureBytes));

		ImGui::RegisterUserTexture(data.get());

		return std::make_unique<Texture>(*this, std::move(data));
	}

	int ImGuiTextureFactory::GetMaximumSide() const
	{
		return m_MaximumSide;
	}

	void ImGuiTextureFactory::CollectRetired()
	{
		const auto destroyed = std::ranges::partition(m_Retired, [](const std::unique_ptr<ImTextureData>& data) {
			return data->Status != ImTextureStatus_Destroyed;
		});

		for (const std::unique_ptr<ImTextureData>& data : destroyed)
		{
			Unregister(*data);
		}

		m_Retired.erase(destroyed.begin(), destroyed.end());
	}

	void DrawTexture(ImDrawList& drawList, const ImageTexture& texture, const ImRect& where, TextureSampling sampling)
	{
		const ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();

		// Both or neither: a switch to nearest with no way back would leave the rest of the frame nearest.
		const bool switchesSampler = sampling == TextureSampling::Nearest &&
									 platformIo.DrawCallback_SetSamplerNearest != nullptr &&
									 platformIo.DrawCallback_SetSamplerLinear != nullptr;

		if (switchesSampler)
		{
			drawList.AddCallback(platformIo.DrawCallback_SetSamplerNearest, nullptr);
		}

		drawList.AddImage(texture.GetRef(), where.Min, where.Max);

		if (switchesSampler)
		{
			drawList.AddCallback(platformIo.DrawCallback_SetSamplerLinear, nullptr);
		}
	}
}
