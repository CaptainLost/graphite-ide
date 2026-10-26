#include "Image/ImageStatus.h"

#include "Core/Base/ByteSize.h"

#include <format>
#include <string>

namespace Graphite {

	std::vector<ViewStatusItem> ImageStatusItems(const ImageFacts& facts, const std::optional<HoveredPixel>& hovered)
	{
		std::vector<ViewStatusItem> items;

		if (hovered) {
			items.push_back({std::format("{}, {}", hovered->At.X, hovered->At.Y), false,
			                 IM_COL32(hovered->R, hovered->G, hovered->B, hovered->A)});
			items.push_back({std::format("#{:02X}{:02X}{:02X}{:02X}", hovered->R, hovered->G, hovered->B, hovered->A), true,
			                 std::nullopt});
		}

		std::string text = std::format("{} × {} · {}", facts.Width, facts.Height, Core::ImageFormatName(facts.Format));
		if (facts.EntryCount > 1)
			text += std::format(" · {} sizes", facts.EntryCount);
		if (facts.FileFrameCount > 1) {
			if (facts.ShownFrameCount < facts.FileFrameCount)
				text += std::format(" · first of {} frames", facts.FileFrameCount);
			else
				text += std::format(" · {} frames", facts.FileFrameCount);
		}
		text += " · " + Core::FormatByteSize(facts.FileSize);

		items.push_back({std::move(text), false, std::nullopt});
		return items;
	}
}
