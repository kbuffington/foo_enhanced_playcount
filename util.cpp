#include "stdafx.h"

#include "util.h"

namespace util {

	// Converts a character string to a wide character string.
	//
	// Parameters:
	//   str  the character string to convert
	//
	// Returns:
	//   A wide character string.
	std::wstring str2wstr(std::string str) {
		int size = mbstowcs(NULL, str.c_str(), 0);

		wchar_t * str_wcs = (wchar_t *)_alloca((size + 1) * sizeof(wchar_t));
		mbstowcs(str_wcs, str.c_str(), size + 1);

		return std::wstring(str_wcs);
	}

	// Converts a wide character string to a character string.
	//
	// Parameters:
	//   wstr  the wide character string to convert
	//
	// Returns:
	//   A character string.
	std::string wstr2str(std::wstring wstr) {
		int size = wcstombs(NULL, wstr.c_str(), 0);

		char *str_mbs = (char *)_alloca((size + 1) * sizeof(char));
		wcstombs(str_mbs, wstr.c_str(), size + 1);

		return std::string(str_mbs);
	}

	t_uint64 timestampWindowsToJS(t_filetimestamp t) {
		return pfc::fileTimeWtoU(t) * 1000;
	}
}