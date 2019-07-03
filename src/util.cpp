#include "stdafx.h"

#include "util.h"

namespace util {

	t_uint64 timestampWindowsToJS(t_filetimestamp t) {
		return pfc::fileTimeWtoU(t) * 1000;
	}
}