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
#include <chrono>

// https://stackoverflow.com/questions/4913922/possible-problems-with-nominmax-on-visual-c
// these are needed to get rapidjson to work
#define NOMINMAX
namespace Gdiplus
{
	using std::min;
	using std::max;
};

#include <foobar2000/helpers/foobar2000+atl.h>
#include <foobar2000/helpers/filetimetools.h>
