#include "corepch.h"

#include "Core/ImGui/Animation.h"

#include "Core/App/Redraw.h"

#include <algorithm>
#include <cmath>

#include <im_anim.h>
#include <imgui_internal.h>

namespace Core {

	namespace {
		constexpr float kFadeSettledEpsilon = 0.001f;

		ImGuiID HoverChannel()
		{
			static const ImGuiID channel = ImHashStr("Core::Animation::Hover");
			return channel;
		}

		float SmoothStep(float progress)
		{
			return progress * progress * (3.0f - 2.0f * progress);
		}

		float Lerp(float from, float to, float amount)
		{
			return from + (to - from) * amount;
		}

		// WARNING: DeltaTime carries the whole sleep on the first frame after the loop slept, so a fade starting on that frame would arrive at once if the step were not capped.
		float CappedFrameStepSeconds()
		{
			return std::min(ImGui::GetIO().DeltaTime, Animation::kLongestStepSeconds);
		}

		void RequestNextFrame()
		{
			Redraw::RequestIn(Redraw::Clock::duration::zero());
		}
	}

	// WARNING: deliberately no iam_gc: it ages a tween from the frame it was created, not the frame it was last asked for, so it drops a hover that is still in use.
	void Animation::BeginFrame()
	{
		iam_update_begin_frame();
	}

	float Animation::HoverAmount(ImGuiID id, bool hovered)
	{
		const float targetAmount = hovered ? 1.0f : 0.0f;
		const float fadeSeconds = hovered ? kHoverInSeconds : kHoverOutSeconds;

		const float frameStepSeconds = CappedFrameStepSeconds();

		const float amount = iam_tween_float(id, HoverChannel(), targetAmount, fadeSeconds,
											 iam_ease_preset(iam_ease_out_quad),
											 iam_policy_crossfade, frameStepSeconds);

		const bool fadeUnderway = std::fabs(amount - targetAmount) > kFadeSettledEpsilon;
		if (fadeUnderway)
		{
			RequestNextFrame();
		}

		return amount;
	}

	// WARNING: kept in the window's state storage rather than in ImAnim: ImAnim starts every tween from one fixed value, so a switch first drawn on would slide in from off.
	float Animation::SwitchAmount(ImGuiID id, bool on)
	{
		const float targetAmount = on ? 1.0f : 0.0f;
		float* knobProgress = ImGui::GetStateStorage()->GetFloatRef(id, targetAmount);

		const float knobStep = CappedFrameStepSeconds() / kSwitchSeconds;

		if (*knobProgress < targetAmount)
		{
			*knobProgress = std::min(*knobProgress + knobStep, targetAmount);
		}
		else if (*knobProgress > targetAmount)
		{
			*knobProgress = std::max(*knobProgress - knobStep, targetAmount);
		}

		const bool knobMoving = *knobProgress != targetAmount;
		if (knobMoving)
		{
			RequestNextFrame();
		}

		return SmoothStep(*knobProgress);
	}

	ImVec4 Animation::Blend(const ImVec4& from, const ImVec4& to, float amount)
	{
		return ImVec4(Lerp(from.x, to.x, amount), Lerp(from.y, to.y, amount),
					  Lerp(from.z, to.z, amount), Lerp(from.w, to.w, amount));
	}
}
