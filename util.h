#pragma once

#include <string>
#include <stdint.h>

namespace util {

	std::wstring str2wstr(std::string str);

	std::string wstr2str(std::wstring wstr);

	t_uint64 timestampWindowsToJS(t_uint64 t);
}
