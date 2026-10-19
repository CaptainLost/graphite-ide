#include "UI/InfoBars/RestrictedModeBar.h"

#include "UI/InfoBars/InfoBar.h"

#include "Core/FileSystem/FileSystem.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <string>

namespace Graphite {

	RestrictedBarChoice DrawRestrictedModeBar(const std::optional<std::filesystem::path>& parent, const ImVec4& warningColour)
	{
		constexpr std::size_t kTrustFolder = 0;

		InfoBarContent content{.Id = "##RestrictedMode",
							   .Weight = InfoBarWeight::Warning,
							   .Glyph = ICON_LC_SHIELD_ALERT,
							   .WeightColour = warningColour,
							   .Message = "Restricted mode: the language server and background analysis are off for this folder.",
							   .Buttons = {{.Label = "Trust Folder", .Primary = true}},
							   .CloseTooltip = "Hide until Graphite restarts"};

		if (parent)
		{
			content.Buttons.push_back({.Label = "Trust All in " + Core::ToUtf8(*parent)});
		}

		const InfoBarAnswer answer = DrawInfoBar(content);

		if (answer.Closed)
		{
			return RestrictedBarChoice::Close;
		}

		if (!answer.Pressed.has_value())
		{
			return RestrictedBarChoice::None;
		}

		return *answer.Pressed == kTrustFolder ? RestrictedBarChoice::TrustFolder : RestrictedBarChoice::TrustParent;
	}

	std::string TrustAllMessage(const std::filesystem::path& parent)
	{
		return "Trust every folder under\n" + Core::ToUtf8(parent) +
			   "\n\nEach folder in it, now and in the future, will run the language server and "
			   "background analysis when opened.";
	}
}
