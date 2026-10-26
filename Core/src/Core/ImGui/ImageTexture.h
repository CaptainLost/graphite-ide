#pragma once

#include "Core/Base/NonCopyable.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <memory>
#include <vector>

namespace Core {

	struct RgbaImage;

	enum class TextureSampling
	{
		Smooth,
		Nearest
	};

	// A picture on the GPU for as long as its handle lives. Textures are made, drawn and released on the UI thread only.
	class ImageTexture : NonCopyable
	{
	public:
		virtual ~ImageTexture() = default;

		virtual ImTextureRef GetRef() const = 0;

		virtual ImVec2 GetSize() const = 0;
	};

	class TextureFactory : NonCopyable
	{
	public:
		virtual ~TextureFactory() = default;

		// The image has to be within GetMaximumSide on both sides already; the factory does not scale it.
		virtual std::unique_ptr<ImageTexture> Create(const RgbaImage& image) = 0;

		virtual int GetMaximumSide() const = 0;
	};

	// Textures registered with ImGui as user textures, which the renderer backend uploads and frees as it renders.
	// WARNING: every texture has to be released before the factory is destroyed, and the factory destroyed after the
	// renderer backend shuts down, which frees what is still on the GPU, but before the ImGui context is.
	class ImGuiTextureFactory final : public TextureFactory
	{
	public:
		explicit ImGuiTextureFactory(int maximumSide);
		~ImGuiTextureFactory() override;

		std::unique_ptr<ImageTexture> Create(const RgbaImage& image) override;

		int GetMaximumSide() const override;

		// Frees the released textures the backend has destroyed. Called once a frame, after the frame was rendered.
		void CollectRetired();

	private:
		class Texture;

		int m_MaximumSide = 0;
		int m_LiveTextures = 0;
		std::vector<std::unique_ptr<ImTextureData>> m_Retired;
	};

	// Nearest sampling needs the backend's sampler callbacks; a backend without them draws the texture smoothly.
	void DrawTexture(ImDrawList& drawList, const ImageTexture& texture, const ImRect& where, TextureSampling sampling);
}
