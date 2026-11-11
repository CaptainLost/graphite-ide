#include "Sessions/PriceUpdates.h"

#include "Sessions/ClaudeSettings.h"
#include "Sessions/PricingPage.h"

#include "Core/Base/Log.h"

#include <utility>

namespace Graphite {

	namespace {

		Core::Result<PriceList> FetchPricingPage(Core::HttpClient& http)
		{
			const std::string url{kPricingPageUrl};
			Core::Result<Core::HttpResponse> fetched = http.Fetch({.Url = url});

			if (!fetched)
			{
				return fetched.GetError();
			}

			const Core::HttpResponse& response = fetched.GetValue();

			if (response.Status == Core::kHttpStatusProxyAuthenticationRequired)
			{
				return Core::ProxyCredentialsError(url);
			}

			if (response.Status != Core::kHttpStatusOk)
			{
				return Core::Error{.Code = Core::ErrorCodeOfHttpStatus(response.Status),
								   .Message = Core::DescribeRefusedDownload(url, response.Status)};
			}

			return ReadPricingPage(response.Body);
		}
	}

	PriceUpdates::PriceUpdates(ClaudeSettings& settings, Core::Ref<Core::HttpClient> http, Core::JobSubmitter submit, Today today)
		: m_Settings(settings), m_Http(std::move(http)), m_Answer(std::move(submit)), m_Today(std::move(today))
	{
	}

	void PriceUpdates::FetchIfNeverFetched()
	{
		if (m_Settings.GetPricesFetchedOn().empty())
		{
			Fetch();
		}
	}

	void PriceUpdates::FetchForUnpricedModel()
	{
		// WARNING: An old transcript may name a model the page will never list, such as a retired one, so it would
		// otherwise ask again at every start.
		const bool fetchedToday = m_Settings.GetPricesFetchedOn() == m_Today();

		if (!m_FetchedThisRun && !fetchedToday)
		{
			Fetch();
		}
	}

	void PriceUpdates::Fetch()
	{
		if (m_Http == nullptr || m_Answer.IsInFlight())
		{
			return;
		}

		m_FetchedThisRun = true;
		m_Answer.Ask([http = m_Http](const Core::CancellationToken&) { return FetchPricingPage(*http); });
	}

	void PriceUpdates::Poll()
	{
		std::optional<Core::Result<PriceList>> landed = m_Answer.TakeNewest();

		if (!landed.has_value())
		{
			return;
		}

		if (!*landed)
		{
			m_Problem = landed->GetError().Message;
			APP_WARN("Claude prices were not fetched: {}", *m_Problem);
			return;
		}

		m_Problem.reset();
		m_Settings.AdoptFetchedPrices(landed->GetValue(), m_Today());
	}
}
