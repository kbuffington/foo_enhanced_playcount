#include "stdafx.h"
#include "globals.h"
#include "query.h"
#include "lastfm.h"
#include <sstream>
#include <vector>
#include <locale>
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
	album = "Up Close";
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
	int maxPages = stoi(config.LruCacheSize.c_str()) - 1;	// limit requests to greater of 5 or cache size - 1
	if (maxPages < 5) {
		maxPages = 5;
	}

	while (configured && !done && page <= maxPages) {
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
		}
		auto buf = query->perform(hash);

		done = parseJson(buf, playTimes, lastPlayed);
	}

	return playTimes;
}

void remove_punct(char *p)
{
	char *src = p, *dst = p;
	std::locale loc;

	while (*src) {
		if (std::ispunct((unsigned char)*src, loc) && *src != '&' && *src != '(' && *src != ')') {
			/* Skip this character */
			src++;
		} else if (src == dst) {
			/* Increment both pointers without copying */
			src++;
			dst++;
		} else {
			/* Copy character */
			*dst++ = *src++;
		}
	}

	*dst = 0;
}

bool fieldsEq(pfc::string8 songInfo, const pfc::string8 value) {
	char * info; 
	char * val; 
	info = (char*)malloc(sizeof(char) * (songInfo.length() + 1));
	val = (char*)malloc(sizeof(char) * (value.length() + 1));
	strcpy_s(info, songInfo.length() + 1, songInfo.c_str());
	strcpy_s(val, value.length() + 1, value.c_str());
	remove_punct(info);
	remove_punct(val);

	return stringCompareCaseInsensitive(info, val) == 0;
}

bool Lastfm::parseJson(const pfc::string8 buffer, std::vector<t_filetimestamp>& playTimes, t_uint64 lastPlayed) {
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
							(!config.CompareAlbumFields || fieldsEq(album, lfmAlbum)) &&
							fieldsEq(title, lfmTitle)) {

							if (!config.RemoveDuplicateLastfmScrobbles || time < lastRecordedTime - 29) {
								/* last.fm occasionally will double count songs, giving each one a timestamp
									* one second apart. Skip all times that are scrobbled less than 30 seconds apart because
									* you can't scrobble a song less than 30 seconds long so it must be a false play. Plays
									* are listed most recent first so we have to subtract.
									*/
								lastRecordedTime = time;
								playTimes.insert(playTimes.begin(), fileTimeUtoW(time + 60));	// Add 1 minute to start time to match foobar's timestamp
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

	return done || count < 200;
}
