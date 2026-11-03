#pragma once

#include "Editor/Overlays/SemanticOverlay.h"

#include "Language/Api/LanguageId.h"
#include "Language/Server/LanguageServices.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	class PreviewServerPort
	{
	public:
		virtual ~PreviewServerPort() = default;

		PreviewServerPort() = default;
		PreviewServerPort(const PreviewServerPort&) = delete;
		PreviewServerPort& operator=(const PreviewServerPort&) = delete;

		virtual void Open(const std::filesystem::path& path, LanguageId language,
						  std::string text) = 0;
		virtual LanguageServices::Ticket Ask(const std::filesystem::path& path,
											 LanguageId language) = 0;
		virtual void Close(const std::filesystem::path& path, LanguageId language) = 0;
	};

	class PreviewSemantics
	{
	public:
		explicit PreviewSemantics(PreviewServerPort& port) : m_Port(port) {}

		~PreviewSemantics();

		PreviewSemantics(const PreviewSemantics&) = delete;
		PreviewSemantics& operator=(const PreviewSemantics&) = delete;

		void Tick(std::size_t frame, double now);

		void Declare(const std::filesystem::path& path, const Core::TextBuffer& buffer,
					 std::size_t tabWidth, bool editorHolds);

		bool Holds(const std::filesystem::path& path) const;

		const std::vector<SemanticSpan>& SpansOn(const std::filesystem::path& path,
												 std::size_t line) const;

		bool Apply(const LanguageServices::Answer& answer);

	private:
		struct Entry
		{
			SemanticOverlay Overlay;

			Core::TextBuffer Snapshot;

			std::size_t TabWidth = Core::DefaultTabWidth;
			std::size_t Revision = 0;

			bool EditorHolds = false;

			std::optional<LanguageServices::Ticket> Ticket;

			double NextAskAt = 0.0;

			std::size_t LastDeclared = 0;
		};

		using EntryMap = std::map<std::filesystem::path, Entry>;

		void Ask(const std::filesystem::path& path, Entry& entry);

		PreviewServerPort& m_Port;

		EntryMap m_Entries;

		std::size_t m_Frame = 0;
		double m_Now = 0.0;
	};
}
