#include "Core/Core.h"

namespace Core
{
    const char* GetName()
    {
        CORE_DEBUG("Core name requested");
        return "Core";
    }
} // namespace Core
