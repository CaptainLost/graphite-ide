#include "corepch.h"

#include "Core/ImGui/Animation.h"

#include "Core/App/Redraw.h"

#include <algorithm>
#include <cmath>

#include <im_anim.h>
#include <imgui_internal.h>

namespace Core {

	namespace {
		// Closer than this to its target a fade is finished: a thousandth of a colour channel
		// is below what a display shows.
		constexpr float kSettledEpsilon = 0.001f;

		// ImAnim keys a tween by item and channel; every hover shares this channel, so an item
		// animating something else under its own id does not collide with its hover.
		ImGuiID HoverChannel()
		{
			static const ImGuiID channel = ImHashStr("Core::Animation::Hover");
			return channel;
		}

		float SmoothStep(float progress)
		{
			return progress * progress * (3.0f - 2.0f * progress);
		}
	}

	void Animation::BeginFrame()
	{
		// No iam_gc: it ages a tween from the frame it was created, not the frame it was last
		// asked for, so it drops a hover still in use - an item held under the pointer longer
		// than the age limit fades out and back in. A tween only exists for an item that has
		// been hovered at least once, so keeping them all costs little.
		iam_update_begin_frame();
	}

	float Animation::HoverAmount(ImGuiID id, bool hovered)
	{
		const float target = hovered ? 1.0f : 0.0f;
		const float seconds = hovered ? kHoverInSeconds : kHoverOutSeconds;

		// Crossfade rather than queue: a pointer that crosses an item and comes back mid-fade
		// turns the fade around from where it is instead of finishing it first.
		// The step is capped because the first frame after the loop has slept carries the
		// whole sleep in DeltaTime, and a fade starting on that frame would arrive at once:
		// whether a hover faded would depend on how long the pointer had rested. A fade asks
		// for its own frames below, so the cap only ever shortens that first step.
		const float step = std::min(ImGui::GetIO().DeltaTime, kLongestStepSeconds);

		const float amount = iam_tween_float(id, HoverChannel(), target, seconds,
											 iam_ease_preset(iam_ease_out_quad),
											 iam_policy_crossfade, step);

		// A fade still under way asks for the next frame itself. It usually starts on pointer
		// movement, whose frames outlast it anyway; but one started by the layout shifting
		// under a still pointer, on a frame no event asked for, would otherwise freeze
		// mid-way until the loop's idle tick.
		if (std::fabs(amount - target) > kSettledEpsilon)
		{
			Redraw::RequestIn(Redraw::Clock::duration::zero());
		}

		return amount;
	}

	// Kept in the window's state storage rather than in ImAnim: ImAnim starts every tween from one
	// fixed value, so a switch first drawn on would slide in from off as its page opened.
	float Animation::SwitchAmount(ImGuiID id, bool on)
	{
		const float target = on ? 1.0f : 0.0f;
		float* progress = ImGui::GetStateStorage()->GetFloatRef(id, target);

		const float step = std::min(ImGui::GetIO().DeltaTime, kLongestStepSeconds) / kSwitchSeconds;

		if (*progress < target)
		{
			*progress = std::min(*progress + step, target);
		}
		else if (*progress > target)
		{
			*progress = std::max(*progress - step, target);
		}

		const bool moving = *progress != target;

		if (moving)
		{
			Redraw::RequestIn(Redraw::Clock::duration::zero());
		}

		return SmoothStep(*progress);
	}

	ImVec4 Animation::Blend(const ImVec4& from, const ImVec4& to, float amount)
	{
		return ImVec4(from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount,
					  from.z + (to.z - from.z) * amount, from.w + (to.w - from.w) * amount);
	}
}
