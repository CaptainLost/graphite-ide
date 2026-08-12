#pragma once

#include "Core/Command/Chord.h"

#include <string>

namespace Core
{
    /// One chord two commands both answer to. Which of them a press actually reaches is
    /// then a question about iteration order, which is no answer at all.
    struct Conflict
    {
        Chord chord;
        std::string first;
        std::string second;
    };
}
