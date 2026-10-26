#pragma once

#include "Core/Image/RgbaImage.h"
#include "Core/ImGui/ImageTexture.h"

#include <imgui.h>

#include <memory>
#include <utility>

// A texture with no GPU behind it, drawn under its id, for tests that run without a renderer.
class FakeImageTexture final : public Core::ImageTexture
{
public:
	FakeImageTexture(ImTextureID id, ImVec2 size, std::shared_ptr<int> released = nullptr)
		: m_Id(id), m_Size(size), m_Released(std::move(released))
	{
	}

	~FakeImageTexture() override
	{
		if (m_Released)
		{
			++*m_Released;
		}
	}

	ImTextureRef GetRef() const override { return ImTextureRef(m_Id); }

	ImVec2 GetSize() const override { return m_Size; }

	ImTextureID GetId() const { return m_Id; }

private:
	ImTextureID m_Id = ImTextureID_Invalid;
	ImVec2 m_Size;
	std::shared_ptr<int> m_Released;
};

// Hands out fake textures, each under an id of its own, and counts how many it made and how many were released.
// The count is shared, so a texture may outlive the factory.
class FakeTextureFactory final : public Core::TextureFactory
{
public:
	static constexpr int kDefaultMaximumSide = 16384;
	static constexpr ImTextureID kFirstTextureId = 1000;

	explicit FakeTextureFactory(int maximumSide = kDefaultMaximumSide) : m_MaximumSide(maximumSide) {}

	std::unique_ptr<Core::ImageTexture> Create(const Core::RgbaImage& image) override
	{
		const ImTextureID id = kFirstTextureId + static_cast<ImTextureID>(m_Created);
		++m_Created;

		return std::make_unique<FakeImageTexture>(
			id, ImVec2(static_cast<float>(image.Width), static_cast<float>(image.Height)), m_Released);
	}

	int GetMaximumSide() const override { return m_MaximumSide; }

	int GetCreatedCount() const { return m_Created; }

	int GetReleasedCount() const { return *m_Released; }

	int GetLiveCount() const { return m_Created - *m_Released; }

private:
	int m_MaximumSide = kDefaultMaximumSide;
	int m_Created = 0;
	std::shared_ptr<int> m_Released = std::make_shared<int>(0);
};
