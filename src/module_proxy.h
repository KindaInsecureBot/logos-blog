#pragma once
#ifdef BUILD_TESTS
#include "../tests/module_proxy.h"
#else
#include <logos_api_client.h>
using ModuleProxy = LogosAPIClient;
#endif
