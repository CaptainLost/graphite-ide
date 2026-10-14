#include <doctest/doctest.h>

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/Checkbox.h"

#include <imgui.h>

#include <cstddef>

namespace {

	constexpr float kFrameSeconds = 1.0f / 60.0f;
	const ImVec2 kCheckboxPosition(20.0f, 30.0f);
	const ImVec4 kChangedOutline(0.9f, 0.6f, 0.2f, 1.0f);

	enum class OutlineChoice
	{
		Style,
		Custom
	};

	// One checkbox in a window of its own, drawn a frame at a time as the application draws it.
	class CheckboxFrames
	{
	public:
		explicit CheckboxFrames(const char* label, OutlineChoice outline = OutlineChoice::Style)
			: m_Label(label), m_Outline(outline)
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

		~CheckboxFrames() { ImGui::DestroyContext(m_Context); }

		CheckboxFrames(const CheckboxFrames&) = delete;
		CheckboxFrames& operator=(const CheckboxFrames&) = delete;

		void Next(bool& value) { Draw(value, DrawMode::Normal); }

		void NextFocusing(bool& value) { Draw(value, DrawMode::Focus); }

		void NextDisabled(bool& value) { Draw(value, DrawMode::Disabled); }

		// A point inside the box, at its centre.
		void ClickBox(bool& value) { ClickAt(m_BoxCentre, value, DrawMode::Normal); }

		// A point on the label, a few pixels short of the item's right edge.
		void ClickLabel(bool& value) { ClickAt(m_LabelPoint, value, DrawMode::Normal); }

		void ClickBoxDisabled(bool& value) { ClickAt(m_BoxCentre, value, DrawMode::Disabled); }

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

		float GetReportedWidth() const { return m_ReportedWidth; }

	private:
		enum class DrawMode
		{
			Normal,
			Focus,
			Disabled
		};

		void ClickAt(const ImVec2& point, bool& value, DrawMode mode)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddMousePosEvent(point.x, point.y);
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
			ImGui::Begin("checkbox");
			ImGui::SetCursorScreenPos(kCheckboxPosition);

			if (mode == DrawMode::Focus)
			{
				ImGui::SetKeyboardFocusHere();
			}

			ImGui::BeginDisabled(mode == DrawMode::Disabled);
			const bool flipped = DrawCheckbox(value);
			ImGui::EndDisabled();

			if (flipped)
			{
				++m_Flips;
			}

			const ImVec2 itemMin = ImGui::GetItemRectMin();
			const ImVec2 itemMax = ImGui::GetItemRectMax();
			const float side = Core::kCheckboxSize;
			m_BoxCentre = ImVec2(itemMin.x + side * 0.5f, (itemMin.y + itemMax.y) * 0.5f);
			m_LabelPoint = ImVec2(itemMax.x - 3.0f, (itemMin.y + itemMax.y) * 0.5f);
			m_ItemSize = ImVec2(itemMax.x - itemMin.x, itemMax.y - itemMin.y);
			m_ReportedWidth = Core::CheckboxWidth(m_Label);

			ImGui::End();
			ImGui::EndFrame();
		}

		bool DrawCheckbox(bool& value)
		{
			switch (m_Outline)
			{
			case OutlineChoice::Style:
				return Core::Checkbox(m_Label, &value);
			case OutlineChoice::Custom:
				return Core::Checkbox(m_Label, &value, kChangedOutline);
			}

			return false;
		}

		const char* m_Label = nullptr;
		OutlineChoice m_Outline = OutlineChoice::Style;
		ImGuiContext* m_Context = nullptr;
		ImVec2 m_BoxCentre;
		ImVec2 m_LabelPoint;
		ImVec2 m_ItemSize;
		float m_ReportedWidth = 0.0f;
		std::size_t m_Flips = 0;
	};
}

TEST_CASE("A click on a checkbox's box flips it once")
{
	CheckboxFrames frames{"Auto-scroll"};
	bool value = false;
	frames.Next(value);

	frames.ClickBox(value);

	CHECK(value);
	CHECK(frames.GetFlipCount() == 1);
}

TEST_CASE("A click on a checkbox's label flips it too")
{
	CheckboxFrames frames{"Auto-scroll"};
	bool value = true;
	frames.Next(value);

	frames.ClickLabel(value);

	CHECK_FALSE(value);
	CHECK(frames.GetFlipCount() == 1);
}

TEST_CASE("A focused checkbox flips on Space")
{
	CheckboxFrames frames{"Auto-scroll"};
	bool value = false;
	frames.NextFocusing(value);
	frames.Next(value);

	frames.PressKey(ImGuiKey_Space, value);

	CHECK(value);
}

TEST_CASE("A disabled checkbox does not flip on a click")
{
	CheckboxFrames frames{"Auto-scroll"};
	bool value = false;
	frames.NextDisabled(value);

	frames.ClickBoxDisabled(value);

	CHECK_FALSE(value);
	CHECK(frames.GetFlipCount() == 0);
}

TEST_CASE("A checkbox is as tall as a frame and as wide as its box and spacing and label")
{
	CheckboxFrames frames{"Auto-scroll"};
	bool value = false;

	frames.Next(value);

	const float expected =
		Core::kCheckboxSize + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Auto-scroll").x;
	CHECK(frames.GetItemSize().x == doctest::Approx(expected));
	CHECK(frames.GetItemSize().y == doctest::Approx(ImGui::GetFrameHeight()));
	CHECK(frames.GetReportedWidth() == doctest::Approx(expected));
}

TEST_CASE("A checkbox whose label is only an id is as wide as its box")
{
	CheckboxFrames frames{"##viewed"};
	bool value = false;

	frames.Next(value);

	CHECK(frames.GetItemSize().x == doctest::Approx(Core::kCheckboxSize));
	CHECK(frames.GetReportedWidth() == doctest::Approx(Core::kCheckboxSize));
}

TEST_CASE("A checkbox with its own outline colour flips like any other")
{
	CheckboxFrames frames{"##viewed", OutlineChoice::Custom};
	bool value = false;
	frames.Next(value);

	frames.ClickBox(value);

	CHECK(value);
	CHECK(frames.GetFlipCount() == 1);
}
