#pragma once

#include <vector>
#include "rapidjson/document.h"

namespace foo_enhanced_playcount {
	class Lastfm {
	public:
		Lastfm::Lastfm();
		std::vector<t_filetimestamp> queryLastfm(pfc::string8 trackartist, pfc::string8 trackalbum, pfc::string8 tracktitle);

	private:
		//std::vector<t_filetimestamp> parseJson(const pfc::string8 buffer);
		bool parseJson(const pfc::string8 buffer, std::vector<t_filetimestamp>& playTimes);

		pfc::string8 artist;
		pfc::string8 album;
		pfc::string8 title;
		pfc::string8 user;
	};
}

