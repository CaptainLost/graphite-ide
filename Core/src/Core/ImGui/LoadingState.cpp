#include "corepch.h"

#include "Core/ImGui/LoadingState.h"

#include "Core/App/Redraw.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imspinner.h>
#include <imspinner_text.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>

namespace Core {

	namespace {

		constexpr const char* kLoaderId = "##loadingState";

		constexpr float kSweepsPerSecond = 0.5f;

		constexpr const char* kLineLoaderId = "##loadingLine";

		constexpr float kSmallestSpinnerRadius = 2.0f;
		constexpr float kLineSpinnerThickness = 2.0f;
		constexpr float kLineSpinnerSpeed = 2.8f;
		constexpr float kLineSpinnerArc = 3.14159265f;

		constexpr double kSecondsBeforeLoaderShows = 0.2;

		constexpr int kNeverDrawn = -2;
		constexpr int kBitsPerHalf = 32;

		constexpr const char* kLoaderWaitSinceId = "##loadingStateWaitSince";
		constexpr const char* kLoaderLastFrameId = "##loadingStateLastFrame";
		constexpr const char* kLineWaitSinceId = "##loadingLineWaitSince";
		constexpr const char* kLineLastFrameId = "##loadingLineLastFrame";

		// WARNING: The storage keeps ints and floats, and a float of ImGui's clock loses a fifth of a second within weeks
		// of uptime, so the time goes in as the two halves of the double's bits.
		ImGuiID HighHalfKey(ImGuiID key)
		{
			return ImHashData(&key, sizeof(key), key);
		}

		void StoreTime(ImGuiStorage& storage, ImGuiID key, double time)
		{
			const std::uint64_t bits = std::bit_cast<std::uint64_t>(time);
			storage.SetInt(key, static_cast<int>(static_cast<std::uint32_t>(bits)));
			storage.SetInt(HighHalfKey(key), static_cast<int>(static_cast<std::uint32_t>(bits >> kBitsPerHalf)));
		}

		double LoadTime(const ImGuiStorage& storage, ImGuiID key)
		{
			const std::uint64_t low = static_cast<std::uint32_t>(storage.GetInt(key));
			const std::uint64_t high = static_cast<std::uint32_t>(storage.GetInt(HighHalfKey(key)));
			return std::bit_cast<double>((high << kBitsPerHalf) | low);
		}

		bool HasWaitedLongEnough(const char* waitSinceId, const char* lastFrameId)
		{
			ImGuiStorage* storage = ImGui::GetStateStorage();
			const ImGuiID waitSinceKey = ImGui::GetID(waitSinceId);
			const ImGuiID lastFrameKey = ImGui::GetID(lastFrameId);

			const int frame = ImGui::GetFrameCount();
			const double now = ImGui::GetTime();
			const int lastFrame = storage->GetInt(lastFrameKey, kNeverDrawn);
			const bool waitGoesOn = lastFrame == frame || lastFrame == frame - 1;

			if (!waitGoesOn)
			{
				StoreTime(*storage, waitSinceKey, now);
			}

			storage->SetInt(lastFrameKey, frame);

			const double secondsWaited = now - LoadTime(*storage, waitSinceKey);

			if (secondsWaited < kSecondsBeforeLoaderShows)
			{
				Redraw::RequestIn(std::chrono::duration<double>(kSecondsBeforeLoaderShows - secondsWaited));
				return false;
			}

			return true;
		}

		// WARNING: An imspinner spinner pads its circle by the frame padding above and below, so the radius leaves that
		// padding out or the row grows taller than a line of text.
		float LineSpinnerRadius()
		{
			constexpr float kRadiusPerDiameter = 0.5f;
			const float framePadding = ImGui::GetStyle().FramePadding.y;
			return std::max(kSmallestSpinnerRadius, ImGui::GetTextLineHeight() * kRadiusPerDiameter - framePadding);
		}

		float CentredStart(float space, float extent)
		{
			constexpr float kHalf = 0.5f;
			return std::max(0.0f, (space - extent) * kHalf);
		}
	}

	void LoadingState(const char* text)
	{
		if (!HasWaitedLongEnough(kLoaderWaitSinceId, kLoaderLastFrameId))
		{
			return;
		}

		const ImVec2 region = ImGui::GetContentRegionAvail();
		const float textWidth = ImGui::CalcTextSize(text).x;

		constexpr float kMinimumRegionWidth = 1.0f;
		constexpr float kRadiusPerDiameter = 0.5f;
		constexpr float kDiametersPerRadius = 2.0f;
		const float radius =
			std::min(textWidth, std::max(region.x, kMinimumRegionWidth)) * kRadiusPerDiameter;
		const float height =
			(radius + ImGui::GetStyle().FramePadding.y) * kDiametersPerRadius;

		const ImVec2 start = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(start.x + CentredStart(region.x, radius * kDiametersPerRadius),
								   start.y + CentredStart(region.y, height)));

		const ImColor ink{ImGui::GetStyleColorVec4(ImGuiCol_Text)};
		const ImColor paper{ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)};
		ImSpinner::SpinnerTextShine(kLoaderId, radius, ink, paper, kSweepsPerSecond, text);

		Redraw::RequestIn(Redraw::Clock::duration::zero());
	}

	void LoadingLine(const char* text, WhileLoaderWaits whileWaiting)
	{
		const bool shows = HasWaitedLongEnough(kLineWaitSinceId, kLineLastFrameId);

		if (!shows && whileWaiting == WhileLoaderWaits::ShowNothing)
		{
			ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
			return;
		}

		constexpr float kDiametersPerRadius = 2.0f;
		const float radius = LineSpinnerRadius();
		const ImVec2 rowStart = ImGui::GetCursorScreenPos();
		float spinnerRight = rowStart.x + radius * kDiametersPerRadius;

		if (shows)
		{
			const ImColor ink{ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)};
			const ImColor track{ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)};
			ImSpinner::SpinnerAng(kLineLoaderId, radius, kLineSpinnerThickness, ink, track, kLineSpinnerSpeed,
								  kLineSpinnerArc);
			spinnerRight = ImGui::GetItemRectMax().x;
		}

		// WARNING: The text is placed by position rather than SameLine: SameLine would carry the
		// spinner's frame-padding baseline over and draw the text that much lower than the spinner.
		ImGui::SetCursorScreenPos(ImVec2(spinnerRight + ImGui::GetStyle().ItemSpacing.x, rowStart.y));
		ImGui::TextDisabled("%s", text);

		if (shows)
		{
			Redraw::RequestIn(Redraw::Clock::duration::zero());
		}
	}
}
