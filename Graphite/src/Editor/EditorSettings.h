#pragma once

#include "Git/Diff/DiffViewOptions.h"

#include "Core/ImGui/FontSet.h"
#include "Core/Text/Editing.h"

#include <cstddef>
#include <cstdint>

namespace Core {

	class CommandRegistry;
	class Keymap;
}
namespace Graphite {

	class AppSettings;

	inline constexpr int kDefaultHoverDelayMs = 500;

	enum class DefinitionPreviewMode : std::uint8_t
	{
		Off,
		WithCtrl,
		Always
	};

	struct HoverSettings
	{
		bool ShowInfo = true;
		DefinitionPreviewMode Preview = DefinitionPreviewMode::WithCtrl;
		int DelayMs = kDefaultHoverDelayMs;
	};

	class EditorSettings final
	{
	public:
		explicit EditorSettings(AppSettings& settings) : m_Store(settings) {}

		void Load();

		void RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys);

		float GetFontSize() const { return m_FontSize; }

		void SetFontSize(float size);

		void SetIndentWithSpaces(bool useSpaces);

		void SetIndentWidth(std::size_t width);

		Core::IndentStyle GetIndentStyle() const
		{
			return {m_IndentWithSpaces, m_IndentWidth};
		}

		bool IsWordWrap() const { return m_WordWrap; }

		bool IsDiffWordWrap() const { return m_DiffView.WordWrap; }
		void SetDiffWordWrap(bool wrap);

		const DiffViewOptions& GetDiffViewOptions() const { return m_DiffView; }
		void SetDiffViewOptions(const DiffViewOptions& options);

		bool HasGutterChangeMarkers() const { return m_GutterChangeMarkers; }
		bool HasScrollbarChangeMarkers() const { return m_ScrollbarChangeMarkers; }

		void SetGutterChangeMarkers(bool show);
		void SetScrollbarChangeMarkers(bool show);

		HoverSettings GetHoverSettings() const { return m_Hover; }

		void SetHoverInfo(bool show);
		void SetDefinitionPreview(DefinitionPreviewMode mode);
		void SetHoverDelayMs(int milliseconds);

	private:
		void SetWordWrap(bool wrap);

		AppSettings& m_Store;

		static constexpr float kMinimumFontSize = 8.0f;
		static constexpr float kMaximumFontSize = 32.0f;
		static constexpr float kDefaultFontSize = Core::kCodeFontSize;

		static constexpr float kFontSizeStep = 1.0f;

		static constexpr std::size_t kMinimumIndentWidth = 1;
		static constexpr std::size_t kMaximumIndentWidth = 8;
		static constexpr std::size_t kDefaultIndentWidth = 4;

		float m_FontSize = kDefaultFontSize;

		bool m_IndentWithSpaces = true;
		std::size_t m_IndentWidth = kDefaultIndentWidth;

		bool m_WordWrap = false;
		DiffViewOptions m_DiffView;

		bool m_GutterChangeMarkers = true;
		bool m_ScrollbarChangeMarkers = true;

		static constexpr int kMinimumHoverDelayMs = 200;
		static constexpr int kMaximumHoverDelayMs = 1500;

		HoverSettings m_Hover;
	};
}