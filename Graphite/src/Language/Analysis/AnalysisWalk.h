#pragma once

#include "Core/Base/Base.h"

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace Graphite {

	void CollectAnalysable(const std::filesystem::path& root, const std::filesystem::path& selectedSolution,
						   const Core::CancellationToken& token, const std::function<void(const std::filesystem::path&)>& onFile);

	class AnalysisWalk
	{
	public:
		~AnalysisWalk();

		AnalysisWalk() = default;
		AnalysisWalk(const AnalysisWalk&) = delete;
		AnalysisWalk& operator=(const AnalysisWalk&) = delete;

		void Start(std::filesystem::path root, std::filesystem::path selectedSolution, bool respectGitIgnore);

		void Cancel();

		bool IsWalking() const;

		void Take(std::vector<std::filesystem::path>& destination);

	private:
		// WARNING: Held through a shared_ptr so a walk still winding down after Cancel has a valid
		// block to write to.
		struct Findings
		{
			std::mutex Mutex;
			std::vector<std::filesystem::path> Paths;
			bool Walking = false;
		};

		Core::Ref<Findings> m_Findings = Core::CreateRef<Findings>();
		Core::CancellationToken m_Token;

		bool m_HasUncancelledWalk = false;

		// WARNING: Declared last so the worker is joined before the members it touches are destroyed.
		Core::JobQueue m_Jobs{1};
	};
}
