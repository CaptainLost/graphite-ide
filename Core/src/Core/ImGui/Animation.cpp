#include "corepch.h"

#include "Core/ImGui/Animation.h"

#include <im_anim.h>
#include <imgui_internal.h>

namespace Core::Animation {

	namespace {
		// ImAnim keys a tween by item and channel; every hover shares this channel, so an item
		// animating something else under its own id does not collide with its hover.
		ImGuiID HoverChannel()
		{
			static const ImGuiID channel = ImHashStr("Core::Animation::Hover");
			return channel;
		}
	}

	void BeginFrame()
	{
		// No iam_gc: it ages a tween from the frame it was created, not the frame it was last
		// asked for, so it drops a hover still in use - an item held under the pointer longer
		// than the age limit fades out and back in. A tween only exists for an item that has
		// been hovered at least once, so keeping them all costs little.
		iam_update_begin_frame();
	}

	float HoverAmount(ImGuiID id, bool hovered)
	{
		const float target = hovered ? 1.0f : 0.0f;
		const float seconds = hovered ? kHoverInSeconds : kHoverOutSeconds;

		// Crossfade rather than queue: a pointer that crosses an item and comes back mid-fade
		// turns the fade around from where it is instead of finishing it first.
		return iam_tween_float(id, HoverChannel(), target, seconds,
							   iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade,
							   ImGui::GetIO().DeltaTime);
	}

	ImVec4 Blend(const ImVec4& from, const ImVec4& to, float amount)
	{
		return ImVec4(from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount,
					  from.z + (to.z - from.z) * amount, from.w + (to.w - from.w) * amount);
	}
}
