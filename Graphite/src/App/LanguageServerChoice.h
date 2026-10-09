#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Server/ServerChoice.h"

namespace Graphite {

	class AppSettings;

	ServerChoice ReadServerChoice(const AppSettings& settings, LanguageId language);
}
