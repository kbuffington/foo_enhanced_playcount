#include "stdafx.h"
#include "globals.h"
#include "query.h"
#include "lastfm.h"
#include <sstream>
#include <vector>
#include <set>
#include <locale>
#include "resource.h"
#include "PlaycountConfig.h"
#include "PlayedTimes.h"

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

std::vector<t_filetimestamp> Lastfm::queryByTrack(t_filetimestamp lastPlay) {
	std::vector<t_filetimestamp> playTimes;
	t_uint64 lastPlayed = 0;
	bool done = false;
	int page = 1;
	int maxPages = stoi(config.LruCacheSize.c_str()) - 1;	// limit requests to greater of 5 or cache size - 1
	if (maxPages < 5) {
		maxPages = 5;
	}

	while (configured && !done && page <= maxPages) {
		Query *trackQuery = new Query("user.getTrackScrobbles");
		trackQuery->add_apikey();
		trackQuery->add_param("user", user, false);
		trackQuery->add_param("artist", artist, false);
		trackQuery->add_param("track", title, false);
		trackQuery->add_param("limit", 200);
		trackQuery->add_param("format", "json");
		trackQuery->add_param("page", page++);

		if (lastPlay > 0) {
			// convert to unix timestamp and skip 29 seconds to avoid duplicate scrobbles
			lastPlayed = fileTimeWtoU(lastPlay) + (config.RemoveDuplicateLastfmScrobbles ? 29 : 0);
		}
		//auto buf = query->perform(hash);
		auto buf = trackQuery->perform(0);

		done = parseTrackJson(buf, playTimes, lastPlayed);
	}

	return playTimes;
}

std::vector<scrobbleData> Lastfm::queryRecentTracks(bool newScrobbles, t_filetimestamp timestamp)
{
	std::vector<scrobbleData> m_vec;
	bool done = false;
	int page = 1;
	int maxPages = 5;
	int limit = 100;

	while (configured && !done && page <= maxPages) {
		Query* recentTracksQuery = new Query("user.getRecentTracks");
		recentTracksQuery->add_apikey();
		recentTracksQuery->add_param("user", user, false);
		recentTracksQuery->add_param("limit", limit);
		recentTracksQuery->add_param("format", "json");
		if (newScrobbles) {
			recentTracksQuery->add_param("from", timestamp);
		} else {
			recentTracksQuery->add_param("to", timestamp);
		}
		recentTracksQuery->add_param("page", page++);

		auto buf = recentTracksQuery->perform(0);

		done = parseRecentTracksJson(buf, limit, m_vec);
	}
	if (newScrobbles) {
		std::reverse(m_vec.begin(), m_vec.end());	// reverse newest pulls so we check from oldest to newest
	}
	return m_vec;
}

bool hasNonPunctChars(char *p) {
	char *src = p;
	std::locale loc;

	while (*src) {
		if (!(std::ispunct((unsigned char)*src, loc) && *src != '&' && *src != '(' && *src != ')') && *src != ' ') {
			return true;
		}
		src++;
	}
	return false;
}

void remove_punct(char *p)
{
	char *src = p, *dst = p;
	std::locale loc;
	boolean dstIncremented = false;
	boolean skipping = false;

	while (*src) {
		if ((std::ispunct((unsigned char)*src, loc) && *src != '&' && *src != '(' && *src != ')') ||
			(*src == ' ' && dstIncremented)) {
			/* Skip this character */
			/* skip multiple whitespace */
			src++;
			skipping = true;
		} else if (src == dst) {
			/* Increment both pointers without copying */
			src++;
			dst++;
			dstIncremented = true;
		} else {
			/* Copy character */
			if (skipping) {
				skipping = false;
				*dst++ = ' ';
			}
			*dst++ = *src++;
			dstIncremented = true;
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
	if (hasNonPunctChars(info)) {
		remove_punct(info);
	}
	if (hasNonPunctChars(val)) {
		remove_punct(val);
	}

	return stringCompareCaseInsensitive(info, val) == 0;
}

bool Lastfm::parseTrackJson(const pfc::string8 buffer, std::vector<t_filetimestamp>& playTimes, t_uint64 lastPlayed) {
	Document d;
	d.Parse(buffer);
	int count = 0;
	bool done = false;

	if (!d.HasMember("trackscrobbles")) {
		if (d.HasMember("error") && d.HasMember("message")) {
			FB2K_console_formatter() << "last.fm Error: " << d["message"].GetString();
		}
		return true;
	}
	const Value& a = d["trackscrobbles"];
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

	return done || (count < 200);
}

struct scrobble_compare {
	bool operator() (const scrobbleData& lhs, const scrobbleData& rhs) const {
		stringstream s1, s2;
		s1 << lhs.artist << lhs.title << lhs.album;
		s2 << rhs.artist << rhs.title << rhs.album;
		return s1.str() < s2.str();
	}
};

int unsortedRemoveDuplicates(std::vector<scrobbleData>& scrobbles)
{
	std::set<scrobbleData, scrobble_compare> seenScrobbles; //log(n) existence check

	auto itr = begin(scrobbles);
	while (itr != end(scrobbles))
	{
		if (seenScrobbles.find(*itr) != end(seenScrobbles)) //seen? erase it
			itr = scrobbles.erase(itr); //itr now points to next element
		else
		{
			seenScrobbles.insert(*itr);
			itr++;
		}
	}

	return seenScrobbles.size();
}

bool Lastfm::parseRecentTracksJson(const pfc::string8 buffer, const int limit, std::vector<scrobbleData>& scrobble_vec) {
	bool done = false;
	int count = 0;
	Document d;
	d.Parse(buffer);

	if (!d.HasMember("recenttracks")) {
		if (d.HasMember("error") && d.HasMember("message")) {
			FB2K_console_formatter() << "last.fm Error: " << d["message"].GetString();
		}
	} else {
		const Value& a = d["recenttracks"];
		if (a.IsObject()) {
			if (!a.HasMember("track"))
				return true;
			const Value& t = a["track"];
			if (t.IsArray()) {
				count = t.Size();
				for (SizeType i = 0; i < t.Size(); i++) { // rapidjson uses SizeType instead of size_t.
					const Value& track = t[i];
					if (track.IsObject()) {
						if (track.HasMember("@attr")) {
							const Value& attr = track["@attr"];
							if (attr.IsObject() && attr.HasMember("nowplaying") && fieldsEq(attr["nowplaying"].GetString(), "true")) {
								continue; // skip nowplaying song
							}
						}
						const Value& name = track["name"];
						const Value& al = track["album"];
						const Value& ar = track["artist"];
						const char* date = "";
						t_filetimestamp time = 0;
						if (track.HasMember("date")) {	// shouldn't need this check, but just in case...
							const Value& dt = track["date"];
							if (dt.IsObject()) {
								date = dt["uts"].GetString();
								time = atoi(date);
							}
						}
						bool hasQuotes = false;
						pfc::string8 lfmAlbum = static_cast<pfc::string8>(al["#text"].GetString());
						pfc::string8 lfmArtist = static_cast<pfc::string8>(ar["#text"].GetString());
						pfc::string8 lfmTitle = static_cast<pfc::string8>(name.GetString());
						lfmTitle.replace_string("\"", "$char(34)");
						lfmAlbum.replace_string("\"", "$char(34)");
						if (lfmArtist.replace_string("\\\"", "$char(34)") || lfmArtist.replace_string("\"", "$char(34)")) {
							hasQuotes = true;
						}
					
						if (lfmArtist.length() > 0 && lfmTitle.length() > 0) {
							scrobble_vec.push_back(scrobbleData(lfmTitle, lfmArtist, lfmAlbum, time, hasQuotes));
						}
					}
				}
			}
		}
	}
	unsortedRemoveDuplicates(scrobble_vec);

	return done || (count < limit);
}