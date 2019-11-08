#include "stdafx.h"

#include "util.h"

namespace util {

	t_uint64 timestampWindowsToJS(t_filetimestamp t) {
		FILETIME ft;
		if (FileTimeToLocalFileTime((FILETIME*)& t, &ft)) {
			t_filetimestamp ts = static_cast<__int64>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
			return pfc::fileTimeWtoU(ts) * 1000;
		}
		return 0;
	}
}