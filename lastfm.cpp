#include "stdafx.h"
#include "globals.h"
#include "query.h"
#include "lastfm.h"
#include <sstream>
#include <vector>

#include "rapidjson/document.h"
#include "rapidjson/encodings.h"
#include "rapidjson/stringbuffer.h"

using namespace rapidjson;
using namespace foo_enhanced_playcount;

Lastfm::Lastfm() {
	artist = "";
	album = "";
	title = "";
	user = "MordredKLB";
}

//t_filetimestamp* Lastfm::queryLastfm(pfc::string8 trackartist, pfc::string8 trackalbum, pfc::string8 tracktitle) {

#if 1
std::vector<t_filetimestamp> Lastfm::queryLastfm(pfc::string8 trackartist, pfc::string8 trackalbum, pfc::string8 tracktitle) {
	artist << trackartist;
	album << trackalbum;
	title << tracktitle;

	Query *query = new Query();
	query->add_apikey();
	query->add_param("user", user);
	query->add_param("artist", artist);
	query->add_param("limit", 200);
	query->add_param("format", "json");
	auto buf = query->perform();

	return parseJson(buf);
}

bool fieldsEq(pfc::string8 songInfo, const pfc::string8 value) {
	return _stricmp(songInfo, value) == 0;
}
#endif

std::vector<t_filetimestamp> Lastfm::parseJson(const pfc::string8 buffer) {
	Document d;
	d.Parse(buffer);
	std::vector<t_filetimestamp> playTimes;

	if (!d.HasMember("artisttracks"))
		return playTimes;
	const Value& a = d["artisttracks"];
	if (a.IsObject()) {
		if (!a.HasMember("track"))
			return playTimes;
		const Value& t = a["track"];
		if (t.IsArray()) {
			for (SizeType i = 0; i < t.Size(); i++) { // rapidjson uses SizeType instead of size_t.
				const Value& track = t[i];
				if (track.IsObject()) {
					const Value& name = track["name"];
					const Value& ar = track["artist"];
					const Value& al = track["album"];
					pfc::string8 str;
					pfc::string8 lfmArtist = static_cast<pfc::string8>(ar["#text"].GetString());
					pfc::string8 lfmAlbum = static_cast<pfc::string8>(al["#text"].GetString());
					pfc::string8 lfmTitle = static_cast<pfc::string8>(name.GetString());

					str << lfmArtist << " - " << lfmAlbum << " - " << lfmTitle;

					if (ar.IsObject() && al.IsObject() && 
						fieldsEq(artist, lfmArtist) &&
						fieldsEq(album, lfmAlbum) &&
						fieldsEq(title, lfmTitle)) {

						const Value& dt = track["date"];
						const char * date;
						if (dt.IsObject()) {
							date = dt["uts"].GetString();
							str += " - ";
							//str += date;
						}
						t_filetimestamp time = atoi(date);
						time *= 1000;
						FB2K_console_formatter() << "FOUND: " << str << time;
						time *= 10000;
						time += 116444736000000000;
						playTimes.push_back(time);
												
					} else {
						//FB2K_console_formatter() << "Not found: " << str;
					}
				}
			}
		}
	}

	return playTimes;
}
