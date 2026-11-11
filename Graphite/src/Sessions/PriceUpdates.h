#pragma once

#include "Sessions/Pricing.h"

#include "Core/Base/Pointers.h"
#include "Core/Base/Result.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Core/Jobs/LatestAnswer.h"
#include "Core/Network/HttpClient.h"

#include <functional>
#include <optional>
#include <string>

namespace Graphite {

	class ClaudeSettings;

	// Fetches the pricing page into the settings' fetched prices. There is no schedule: the page is fetched at the
	// first start, when a session names a model with no price (once a run, once a day), and when asked from settings. A failed
	// fetch keeps the prices there were and says why. WARNING: UI thread only; the fetch runs on the submitter.
	class PriceUpdates
	{
	public:
		using Today = std::function<std::string()>;

		// A null client is no network: nothing is fetched, and settings has nothing to offer.
		PriceUpdates(ClaudeSettings& settings, Core::Ref<Core::HttpClient> http, Core::JobSubmitter submit, Today today);

		void FetchIfNeverFetched();

		// WARNING: Once a run and once a day, and not at all once anything has been fetched this run: the page
		// answered already, so a model it does not name is not one asking again would find.
		void FetchForUnpricedModel();

		void Fetch();

		// WARNING: Call once a frame; a fetch that has landed changes the prices here.
		void Poll();

		bool CanFetch() const { return m_Http != nullptr; }

		bool IsFetching() const { return m_Answer.IsInFlight(); }

		const std::optional<std::string>& GetProblem() const { return m_Problem; }

	private:
		ClaudeSettings& m_Settings;
		Core::Ref<Core::HttpClient> m_Http;
		Core::LatestAnswer<Core::Result<PriceList>> m_Answer;
		Today m_Today;

		bool m_FetchedThisRun = false;
		std::optional<std::string> m_Problem;
	};
}
