#include "Core/Version.h"

#include <cstdio>

int main()
{
    std::printf("IDE %s\n", core::Version::String());
    return 0;
}
