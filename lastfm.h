#pragma once

#include <vector>
#include "rapidjson/document.h"

using namespace pfc;

namespace foo_enhanced_playcount {
	class Lastfm {
	public:
		Lastfm::Lastfm(metadb_index_hash hash, string8 trackartist, string8 trackalbum, string8 tracktitle);
		std::vector<t_filetimestamp> queryLastfm(t_filetimestamp lastPlay);

	private:
		bool parseJson(const string8 buffer, std::vector<t_filetimestamp>& playTimes, t_uint64 lastPlayed);

		string8 artist;
		string8 album;
		string8 title;
		string8 user;
		metadb_index_hash hash;
		bool configured;
	};
}

