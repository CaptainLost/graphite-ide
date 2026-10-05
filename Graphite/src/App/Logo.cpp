#include "App/Logo.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace Graphite {

	namespace {
		struct LogoTexture
		{
			Core::RgbaImage Image;
			ColumnSpan Columns;
			// Made on the first draw rather than on load: measuring needs no ImGui context.
			std::unique_ptr<ImTextureData> Texture;
			// The context the texture is registered with. A new context knows nothing of it,
			// and its backend never created it, so it is made again there.
			ImGuiContext* Context = nullptr;
		};

		// One texture for each pixel height asked for. Few ever are — one per interface scale
		// the window has been shown at — so none is ever released before exit, when the
		// renderer backend deletes them with the rest of ImGui's textures.
		LogoTexture& TextureFor(int height)
		{
			static std::map<int, LogoTexture> textures;

			const auto found = textures.find(height);

			if (found != textures.end())
			{
				return found->second;
			}

			LogoTexture& entry = textures[height];
			entry.Image = Core::LoadIconResource(kLogoIcon, height);
			entry.Columns = OpaqueColumns(entry.Image);
			return entry;
		}

		// The height the mark is drawn at for one asked for: the nearest the icon holds an image
		// for, preferring the smaller on a tie. Any other size would be scaled by the system,
		// which loses alpha at every edge and leaves no pixel of the mark fully opaque.
		int PixelHeight(float height)
		{
			static const std::vector<int> sizes = Core::IconResourceSizes(kLogoIcon);
			const int wanted = static_cast<int>(std::lround(height));

			if (wanted <= 0 || sizes.empty())
			{
				return 0;
			}

			int best = sizes.front();

			for (const int size : sizes)
			{
				if (std::abs(size - wanted) < std::abs(best - wanted))
				{
					best = size;
				}
			}

			return best;
		}
	}

	ColumnSpan OpaqueColumns(const Core::RgbaImage& image)
	{
		ColumnSpan span{image.Width, 0};

		for (int y = 0; y < image.Height; ++y)
		{
			for (int x = 0; x < image.Width; ++x)
			{
				const std::size_t alpha = (static_cast<std::size_t>(y) * image.Width + x) * 4 + 3;

				if (image.Pixels[alpha] != 0)
				{
					span.First = std::min(span.First, x);
					span.End = std::max(span.End, x + 1);
				}
			}
		}

		return span.End > span.First ? span : ColumnSpan{};
	}

	float LogoWidth(float height)
	{
		const int pixels = PixelHeight(height);

		if (pixels <= 0)
		{
			return 0.0f;
		}

		const ColumnSpan columns = TextureFor(pixels).Columns;
		return static_cast<float>(columns.End - columns.First);
	}

	void DrawLogo(ImDrawList* draw, ImVec2 centre, float height)
	{
		const int pixels = PixelHeight(height);

		if (pixels <= 0)
		{
			return;
		}

		LogoTexture& entry = TextureFor(pixels);

		if (entry.Columns.End <= entry.Columns.First)
		{
			return;
		}

		if (!entry.Texture || entry.Context != ImGui::GetCurrentContext())
		{
			entry.Context = ImGui::GetCurrentContext();
			entry.Texture = std::make_unique<ImTextureData>();
			entry.Texture->Create(ImTextureFormat_RGBA32, entry.Image.Width, entry.Image.Height);
			std::memcpy(entry.Texture->GetPixels(), entry.Image.Pixels.data(), entry.Image.Pixels.size());
			entry.Texture->UseColors = true;
			// The backend uploads it at the end of the frame, before the draw list that uses
			// it is rendered.
			ImGui::RegisterUserTexture(entry.Texture.get());
		}

		const float size = static_cast<float>(pixels);
		const float width = static_cast<float>(entry.Columns.End - entry.Columns.First);

		// Snapped to whole pixels, so each texel lands on one pixel of the screen and the
		// image stays as sharp as it was exported.
		const ImVec2 min(std::floor(centre.x - width * 0.5f), std::floor(centre.y - size * 0.5f));
		const ImVec2 max(min.x + width, min.y + size);

		// Only the columns the mark paints, so the image's transparent margin takes no room.
		const ImVec2 uvMin(static_cast<float>(entry.Columns.First) / size, 0.0f);
		const ImVec2 uvMax(static_cast<float>(entry.Columns.End) / size, 1.0f);

		draw->AddImage(entry.Texture->GetTexRef(), min, max, uvMin, uvMax);
	}
}
