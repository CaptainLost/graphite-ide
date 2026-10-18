#include <doctest/doctest.h>

#include "Core/ImGui/FontSet.h"
#include "Core/Platform/Process.h"
#include "Core/Text/Utf8.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>

namespace {

	// A context without a backend: building the atlas needs no window.
	struct HeadlessContext
	{
		HeadlessContext() { ImGui::CreateContext(); }
		~HeadlessContext() { ImGui::DestroyContext(); }
	};

	Core::FontSet LoadIntoContext()
	{
		return Core::LoadFontSet(*ImGui::GetIO().Fonts);
	}

	ImWchar CodepointOf(std::string_view utf8)
	{
		std::size_t offset = 0;
		const std::optional<char32_t> codepoint = Core::DecodeUtf8(utf8, offset);
		REQUIRE(codepoint.has_value());
		return static_cast<ImWchar>(codepoint.value_or(Core::kReplacementCharacter));
	}

	bool Draws(ImFont& font, float size, ImWchar codepoint)
	{
		return font.GetFontBaked(size)->FindGlyphNoFallback(codepoint) != nullptr;
	}

	float AdvanceOf(ImFont& font, float size, ImWchar codepoint)
	{
		return font.GetFontBaked(size)->GetCharAdvance(codepoint);
	}

	struct HeadlessFrame
	{
		HeadlessFrame()
			: Context(ImGui::CreateContext())
		{
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.IniFilename = nullptr;
			io.Fonts->Build();
			ImGui::NewFrame();
		}

		~HeadlessFrame()
		{
			ImGui::EndFrame();
			ImGui::DestroyContext(Context);
		}

		HeadlessFrame(const HeadlessFrame&) = delete;
		HeadlessFrame& operator=(const HeadlessFrame&) = delete;

		ImGuiContext* Context;
	};

	bool SymbolFontPresent()
	{
		const Core::Result<std::filesystem::path> fonts = Core::SystemFontsDirectory();
		std::error_code error;
		return fonts && std::filesystem::exists(fonts.GetValue() / "seguisym.ttf", error);
	}
}

TEST_CASE("The interface face is loaded first, at 14 px, and the code face at 15 px")
{
	HeadlessContext context;
	const Core::FontSet set = LoadIntoContext();

	REQUIRE(set.Interface != nullptr);
	REQUIRE(set.Code != nullptr);
	REQUIRE(set.Strong != nullptr);

	CHECK(ImGui::GetIO().Fonts->Fonts[0] == set.Interface);
	CHECK(set.Interface->LegacySize == 14.0f);
	CHECK(set.Code->LegacySize == 15.0f);
	CHECK(set.Strong->LegacySize == 14.0f);

	CHECK(set.Interface != set.Code);
	CHECK(set.Interface != set.Strong);
	CHECK(set.Code != set.Strong);
}

TEST_CASE("Both the interface and the code face draw the icons")
{
	HeadlessContext context;
	const Core::FontSet set = LoadIntoContext();
	REQUIRE(set.Interface != nullptr);
	REQUIRE(set.Code != nullptr);

	const ImWchar close = CodepointOf(ICON_MDI_CLOSE);

	CHECK(Draws(*set.Interface, Core::kInterfaceFontSize, close));
	CHECK(Draws(*set.Code, Core::kCodeFontSize, close));
}

TEST_CASE("The code face is monospaced and the interface face is not")
{
	HeadlessContext context;
	const Core::FontSet set = LoadIntoContext();
	REQUIRE(set.Interface != nullptr);
	REQUIRE(set.Code != nullptr);

	CHECK(AdvanceOf(*set.Code, Core::kCodeFontSize, 'i') == AdvanceOf(*set.Code, Core::kCodeFontSize, 'M'));
	CHECK(AdvanceOf(*set.Interface, Core::kInterfaceFontSize, 'i') <
		  AdvanceOf(*set.Interface, Core::kInterfaceFontSize, 'M'));
}

TEST_CASE("Chrome symbols resolve in the interface face")
{
	HeadlessContext context;
	const Core::FontSet set = LoadIntoContext();
	REQUIRE(set.Interface != nullptr);

	constexpr std::array<ImWchar, 4> kChromeSymbols = {0x00B7, 0x203A, 0x2026, 0x2192};
	const bool symbolFontPresent = SymbolFontPresent();

	for (const ImWchar symbol : kChromeSymbols)
	{
		CAPTURE(symbol);

		const bool drawn = Draws(*set.Interface, Core::kInterfaceFontSize, symbol);

		if (!drawn && !symbolFontPresent)
		{
			MESSAGE("seguisym.ttf is absent and Inter lacks U+", static_cast<unsigned int>(symbol), "; skipped");
			continue;
		}

		CHECK(drawn);
	}
}

TEST_CASE("With no fonts loaded, the push helpers keep the current face and set only the size")
{
	HeadlessFrame frame;

	ImFont* const face = ImGui::GetFont();

	Core::PushCodeFont();
	CHECK(ImGui::GetFont() == face);
	CHECK(ImGui::GetStyle().FontSizeBase == Core::kCodeFontSize);
	ImGui::PopFont();

	Core::PushInterfaceFont();
	CHECK(ImGui::GetFont() == face);
	CHECK(ImGui::GetStyle().FontSizeBase == Core::kInterfaceFontSize);
	ImGui::PopFont();
}

TEST_CASE("The interface font inside a code push is still the interface size")
{
	HeadlessFrame frame;

	const ImGuiStyle& style = ImGui::GetStyle();
	const float scale = style.FontScaleMain * style.FontScaleDpi;
	const int stackDepth = ImGui::GetCurrentContext()->FontStack.Size;
	const float sizeBefore = ImGui::GetFontSize();

	Core::PushCodeFont();
	Core::PushInterfaceFont();
	CHECK(ImGui::GetFontSize() == Core::kInterfaceFontSize * scale);
	ImGui::PopFont();
	ImGui::PopFont();

	CHECK(ImGui::GetCurrentContext()->FontStack.Size == stackDepth);
	CHECK(ImGui::GetFontSize() == sizeBefore);
}
