#include "Language/Server/Adapters/ServerAdapter.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/Adapters/ClangdAdapter.h"

#include <string>
#include <vector>

namespace Graphite {

	namespace {

		constexpr char kSectionSeparator = '.';

		std::vector<std::string_view> KeysOfSection(std::string_view section)
		{
			std::vector<std::string_view> keys;
			std::size_t keyStart = 0;

			for (;;)
			{
				const std::size_t separator = section.find(kSectionSeparator, keyStart);

				if (separator == std::string_view::npos)
				{
					keys.push_back(section.substr(keyStart));
					return keys;
				}

				keys.push_back(section.substr(keyStart, separator - keyStart));
				keyStart = separator + 1;
			}
		}

		nlohmann::json ValueAtSection(const nlohmann::json& settings, std::string_view section)
		{
			const nlohmann::json* current = &settings;

			for (const std::string_view key : KeysOfSection(section))
			{
				if (!current->is_object())
				{
					return nullptr;
				}

				const nlohmann::json::const_iterator member = current->find(std::string{key});

				if (member == current->end())
				{
					return nullptr;
				}

				current = &*member;
			}

			return *current;
		}
	}

	nlohmann::json ServerAdapter::WorkspaceConfiguration(const AdapterContext& context, std::string_view section) const
	{
		if (section.empty())
		{
			return context.Settings;
		}

		return ValueAtSection(context.Settings, section);
	}

	nlohmann::json ServerAdapter::InitializationOptions(const AdapterContext& context) const
	{
		return context.InitializationOptions;
	}

	void ServerAdapter::AfterInitialized(const AdapterContext&, std::vector<OutgoingNotification>&) const
	{
	}

	std::optional<nlohmann::json> ServerAdapter::AnswerRequest(const AdapterContext&, std::string_view, const nlohmann::json&) const
	{
		return std::nullopt;
	}

	std::optional<ProjectProblem> ServerAdapter::ProjectProblemAt(const std::filesystem::path&) const
	{
		return std::nullopt;
	}

	bool ServerAdapter::WarmUpNeedsOpen() const
	{
		return false;
	}

	bool ServerAdapter::SupportsSwitchSourceHeader() const
	{
		return false;
	}

	const ServerAdapter& AdapterOf(AdapterKind kind)
	{
		static const ServerAdapter plain{};
		static const ClangdAdapter clangd{};

		switch (kind)
		{
		case AdapterKind::Plain:
			return plain;
		case AdapterKind::Clangd:
			return clangd;
		}

		return plain;
	}
}
