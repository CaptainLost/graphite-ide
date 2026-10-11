#include <doctest/doctest.h>

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/Toggle.h"

#include <imgui.h>

#include <cstddef>

namespace {

	constexpr float kFrameSeconds = 1.0f / 60.0f;
	const ImVec2 kTogglePosition(20.0f, 30.0f);

	// One toggle in a window of its own, drawn a frame at a time as the application draws it.
	class ToggleFrames
	{
	public:
		ToggleFrames()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(400.0f, 300.0f);
			io.DeltaTime = kFrameSeconds;
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigNavCursorVisibleAlways = true;
			io.Fonts->Build();
		}

		~ToggleFrames() { ImGui::DestroyContext(m_Context); }

		ToggleFrames(const ToggleFrames&) = delete;
		ToggleFrames& operator=(const ToggleFrames&) = delete;

		void Next(bool& value) { Draw(value, DrawMode::Normal); }

		void NextFocusing(bool& value) { Draw(value, DrawMode::Focus); }

		void NextDisabled(bool& value) { Draw(value, DrawMode::Disabled); }

		void Click(bool& value) { ClickDrawing(value, DrawMode::Normal); }

		void ClickDisabled(bool& value) { ClickDrawing(value, DrawMode::Disabled); }

		void PressKey(ImGuiKey key, bool& value)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddKeyEvent(key, true);
			Draw(value, DrawMode::Normal);
			io.AddKeyEvent(key, false);
			Draw(value, DrawMode::Normal);
		}

		std::size_t GetFlipCount() const { return m_Flips; }

		ImVec2 GetItemSize() const { return m_ItemSize; }

	private:
		enum class DrawMode
		{
			Normal,
			Focus,
			Disabled
		};

		void ClickDrawing(bool& value, DrawMode mode)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddMousePosEvent(m_Centre.x, m_Centre.y);
			Draw(value, mode);
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Draw(value, mode);
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Draw(value, mode);
		}

		void Draw(bool& value, DrawMode mode)
		{
			ImGui::NewFrame();
			Core::Animation::BeginFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f));
			ImGui::Begin("toggle");
			ImGui::SetCursorScreenPos(kTogglePosition);

			if (mode == DrawMode::Focus)
			{
				ImGui::SetKeyboardFocusHere();
			}

			ImGui::BeginDisabled(mode == DrawMode::Disabled);
			const bool flipped = Core::Toggle("##toggle", &value);
			ImGui::EndDisabled();

			if (flipped)
			{
				++m_Flips;
			}

			const ImVec2 itemMin = ImGui::GetItemRectMin();
			const ImVec2 itemMax = ImGui::GetItemRectMax();
			m_Centre = ImVec2((itemMin.x + itemMax.x) * 0.5f, (itemMin.y + itemMax.y) * 0.5f);
			m_ItemSize = ImVec2(itemMax.x - itemMin.x, itemMax.y - itemMin.y);

			ImGui::End();
			ImGui::EndFrame();
		}

		ImGuiContext* m_Context = nullptr;
		ImVec2 m_Centre;
		ImVec2 m_ItemSize;
		std::size_t m_Flips = 0;
	};
}

TEST_CASE("A click flips a toggle once")
{
	ToggleFrames frames;
	bool value = false;
	frames.Next(value);

	frames.Click(value);

	CHECK(value);
	CHECK(frames.GetFlipCount() == 1);
}

TEST_CASE("A second click flips a toggle back")
{
	ToggleFrames frames;
	bool value = true;
	frames.Next(value);

	frames.Click(value);
	frames.Click(value);

	CHECK(value);
	CHECK(frames.GetFlipCount() == 2);
}

TEST_CASE("A focused toggle flips on Space")
{
	ToggleFrames frames;
	bool value = false;
	frames.NextFocusing(value);
	frames.Next(value);

	frames.PressKey(ImGuiKey_Space, value);

	CHECK(value);
}

TEST_CASE("A toggle is as wide as the switch and as tall as a frame")
{
	ToggleFrames frames;
	bool value = false;

	frames.Next(value);

	CHECK(frames.GetItemSize().x == doctest::Approx(Core::kToggleWidth));
	CHECK(frames.GetItemSize().y == doctest::Approx(ImGui::GetFrameHeight()));
}

TEST_CASE("A disabled toggle does not flip on a click")
{
	ToggleFrames frames;
	bool value = false;
	frames.NextDisabled(value);

	frames.ClickDisabled(value);

	CHECK_FALSE(value);
	CHECK(frames.GetFlipCount() == 0);
}
