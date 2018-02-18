#include "stdafx.h"
#include "globals.h"
#include "query.h"
#include "lastfm.h"
#include <sstream>
#include <vector>
#include "resource.h"
#include "PlaycountConfig.h"

#include "rapidjson/document.h"
#include "rapidjson/encodings.h"
#include "rapidjson/stringbuffer.h"

using namespace rapidjson;
using namespace foo_enhanced_playcount;
using namespace pfc;

PlaycountConfig const& config{ Config };

Lastfm::Lastfm(metadb_index_hash hashVal, string8 trackartist, string8 trackalbum, string8 tracktitle) {
	user = config.LastfmUsername;
	if (strcmp(DefaultLastfmUsername, user)) {
		hash = hashVal;
		artist << trackartist;
		album << trackalbum;
		title << tracktitle;
		configured = true;
	} else {
		configured = false;
	}

#if 0	/* test values for large results */
	artist = "Eric Johnson";
	album = "Europe Live";
	title = "Fatdaddy";
	user = "joyjoykid";
	configured = true;
#endif
}

std::vector<t_filetimestamp> Lastfm::queryLastfm(t_filetimestamp lastPlay) {
	std::vector<t_filetimestamp> playTimes;
	t_uint64 lastPlayed = 0;
	bool done = false;
	int page = 1;

	while (configured && !done && page <= 5) {	// limit to last 1000 last.fm plays for artist
		Query *query = new Query();
		query->add_apikey();
		query->add_param("user", user);
		query->add_param("artist", artist);
		query->add_param("limit", 200);
		query->add_param("format", "json");
		query->add_param("page", page++);
		if (lastPlay > 0) {
			// convert to unix timestamp and skip 29 seconds to avoid duplicate scrobbles
			lastPlayed = fileTimeWtoU(lastPlay) + (config.RemoveDuplicateLastfmScrobbles ? 29 : 0);
			//query->add_param("startTimestamp", (unsigned int) lastPlayed);
		}
		auto buf = query->perform(hash);

		done = parseJson(buf, playTimes, lastPlayed);
	}

	return playTimes;
}

bool fieldsEq(pfc::string8 songInfo, const pfc::string8 value) {
	return _stricmp(songInfo, value) == 0;
}

bool Lastfm::parseJson(const pfc::string8 buffer, std::vector<t_filetimestamp>& playTimes, t_uint64 lastPlayed) {
	t_filetimestamp start = filetimestamp_from_system_timer();
	Document d;
	d.Parse(buffer);
	int count;
	bool done = false;

	if (!d.HasMember("artisttracks"))
		return true;
	const Value& a = d["artisttracks"];
	if (a.IsObject()) {
		if (!a.HasMember("track"))
			return true;
		const Value& t = a["track"];
		if (t.IsArray()) {
			count = t.Size();
			t_filetimestamp lastRecordedTime = 9999999999999;	// large number
			for (SizeType i = 0; i < t.Size() && !done; i++) { // rapidjson uses SizeType instead of size_t.
				const Value& track = t[i];
				if (track.IsObject()) {
					const Value& dt = track["date"];
					const char * date;
					if (dt.IsObject()) {
						date = dt["uts"].GetString();
					}
					t_filetimestamp time = atoi(date);
					if (time > lastPlayed) {
						const Value& name = track["name"];
						const Value& al = track["album"];
						pfc::string8 str;
						pfc::string8 lfmAlbum = static_cast<pfc::string8>(al["#text"].GetString());
						pfc::string8 lfmTitle = static_cast<pfc::string8>(name.GetString());

						if (al.IsObject() &&
							fieldsEq(album, lfmAlbum) &&
							fieldsEq(title, lfmTitle)) {

							if (!config.RemoveDuplicateLastfmScrobbles || time < lastRecordedTime - 29) {
								/* last.fm occasionally will double count songs, giving each one a timestamp
									* one second apart. Skip all times that are scrobbled less than 30 seconds apart because
									* you can't scrobble a song less than 30 seconds long so it must be a false play. Plays
									* are listed most recent first so we have to subtract.
									*/
								lastRecordedTime = time;
								playTimes.insert(playTimes.begin(), fileTimeUtoW(time));
#ifdef DEBUG
							} else {
								FB2K_console_formatter() << "Skipping double scrobble: " << i << " - " << time << " - " << lastRecordedTime;
#endif
							}
						}
					} else {
						done = true;
					}
				}
			}
		}
	}
//#ifdef DEBUG
//	t_filetimestamp end = filetimestamp_from_system_timer();
//	FB2K_console_formatter() << "Parsing time Elapsed: " << ((float)(end - start)) / 10000000 << " seconds";
//#endif

	return done || count < 200;
}
