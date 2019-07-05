#pragma once

#include <vector>
#include "PlayedTimes.h"
#include "rapidjson/document.h"

using namespace pfc;

namespace foo_enhanced_playcount {
	class Lastfm {
	public:
		Lastfm::Lastfm(metadb_index_hash hash = 0, string8 trackartist = "", string8 trackalbum = "", string8 tracktitle = "");
		std::vector<t_filetimestamp> queryByTrack(t_filetimestamp lastPlay);
		std::vector<scrobbleData> Lastfm::queryRecentTracks();

	private:
		bool parseTrackJson(const string8 buffer, std::vector<t_filetimestamp>& playTimes, t_uint64 lastPlayed);
		std::vector<scrobbleData> parseRecentTracksJson(const string8 buffer);

		string8 artist;
		string8 album;
		string8 title;
		string8 user;
		metadb_index_hash hash;
		bool configured;
	};
}

