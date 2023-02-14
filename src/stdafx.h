#pragma once

#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define WINVER _WIN32_WINNT_WIN7

#include <algorithm>
#include <atomic>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

//#include <nlohmann/json.hpp>
//using json = nlohmann::json;
//

#define NOMINMAX
namespace Gdiplus
{
	using std::min;
	using std::max;
};

#include "resource.h"
#include <foobar2000/helpers/foobar2000+atl.h>
#include <foobar2000/helpers/atl-misc.h>
#include <foobar2000/helpers/filetimetools.h>

#include <ActivScp.h>
#include <ComDef.h>
#include <ShlObj.h>
