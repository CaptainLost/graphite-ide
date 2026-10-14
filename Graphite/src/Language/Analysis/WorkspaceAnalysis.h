#pragma once

#include "Language/Analysis/FileTextSource.h"
#include "Language/Api/LanguageId.h"
#include "Language/Server/LanguageServices.h"

#include "Editor/Overlays/AnalysisCache.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	class AnalysisStore;

	enum class ServerReadiness : std::uint8_t
	{
		Ready,
		ComingUp,
		CannotRun
	};

	ServerReadiness ReadinessFrom(const ServerStatus& status);

	class WorkspaceAnalysisPort
	{
	public:
		virtual ~WorkspaceAnalysisPort() = default;

		WorkspaceAnalysisPort() = default;
		WorkspaceAnalysisPort(const WorkspaceAnalysisPort&) = delete;
		WorkspaceAnalysisPort& operator=(const WorkspaceAnalysisPort&) = delete;

		virtual void Open(const std::filesystem::path& path, LanguageId language,
						  std::string text) = 0;

		virtual LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path& path,
														   LanguageId language) = 0;

		virtual LanguageServices::Ticket AskSymbols(const std::filesystem::path& path,
													LanguageId language) = 0;

		virtual void Close(const std::filesystem::path& path, LanguageId language) = 0;

		virtual ServerReadiness ReadinessOf(const std::filesystem::path& path,
											LanguageId language) = 0;
		virtual bool HasChosenServer(LanguageId language) = 0;
	};

	struct AnalysisProgress
	{
		std::size_t Done = 0;
		std::size_t Total = 0;
		bool Walking = false;
	};

	class WorkspaceAnalysis
	{
	public:
		WorkspaceAnalysis(WorkspaceAnalysisPort& port, FileTextSource& texts, AnalysisCache& cache,
						  AnalysisStore* store);

		~WorkspaceAnalysis();

		WorkspaceAnalysis(const WorkspaceAnalysis&) = delete;
		WorkspaceAnalysis& operator=(const WorkspaceAnalysis&) = delete;

		void SetEnabled(bool enabled);

		bool IsEnabled() const { return m_Enabled; }

		void SetConcurrency(std::size_t limit);

		void SetTabWidth(std::size_t width);

		void SetWalking(bool walking);

		void Enqueue(std::vector<std::filesystem::path> paths);

		void Clear();

		void Tick(double now, bool foregroundBusy,
				  const std::function<bool(const std::filesystem::path&)>& heldElsewhere);

		bool Apply(const LanguageServices::Answer& answer);

		AnalysisProgress Progress() const;

	private:
		static constexpr std::size_t kMinimumConcurrency = 1;

		struct PendingAnalysis
		{
			std::filesystem::path Path;
			LanguageId Language = LanguageId::PlainText;

			std::filesystem::file_time_type ModifiedAt{};

			Core::TextBuffer Snapshot;

			bool Opened = false;

			bool HeldElsewhereAtLastTick = false;
			bool HeldElsewhereAtAnyTick = false;

			std::optional<LanguageServices::Ticket> SemanticTokensTicket;
			std::optional<LanguageServices::Ticket> SymbolsTicket;

			bool HaveSemanticTokens = false;
			bool HaveSymbols = false;

			CachedAnalysis Result;

			double StartedAt = 0.0;
		};

		bool IsOutstanding(const std::filesystem::path& path) const;

		void StartNext(const std::function<bool(const std::filesystem::path&)>& heldElsewhere);

		void BeginArrivedFiles(
			double now, const std::function<bool(const std::filesystem::path&)>& heldElsewhere);

		void FinishCompleted(double now);

		void Drop(std::size_t index);

		void DropAll();

		WorkspaceAnalysisPort& m_Port;
		FileTextSource& m_Texts;
		AnalysisCache& m_Cache;

		AnalysisStore* m_Store = nullptr;

		bool m_Enabled = true;
		bool m_Walking = false;
		std::size_t m_TabWidth = Core::DefaultTabWidth;

		std::map<LanguageId, std::deque<std::filesystem::path>> m_Queues;
		std::size_t m_Done = 0;
		std::size_t m_Queued = 0;

		std::vector<PendingAnalysis> m_Pending;

		std::vector<std::filesystem::path> m_AwaitingText;

		std::size_t m_Limit = kMinimumConcurrency;

		std::optional<double> m_BusySince;

		std::vector<FileText> m_Arrived;
	};
}
