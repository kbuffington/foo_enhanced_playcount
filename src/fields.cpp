#include "stdafx.h"
#include "util.h"
#include "PlaycountConfig.h"
#include <sstream>
#include "PlayedTimes.h"
#include "artistTimes.h"

using namespace foo_enhanced_playcount;

extern PlaycountConfig const& config;

namespace enhanced_playcount_fields {

	std::string getPlayTimesStr(std::vector<t_filetimestamp> playTimes, bool convertTimeStamp, bool jsTimeStamp, bool noArrayChars = false);

	enum provided_fields {
		PLAYED_TIMES,
		PLAYED_TIMES_JS,
		PLAYED_TIMES_RAW,

		LASTFM_PLAYED_TIMES,
		LASTFM_PLAYED_TIMES_JS,
		LASTFM_PLAY_COUNT,
		LASTFM_ADDED,
		LASTFM_FIRST_PLAYED,
		LASTFM_LAST_PLAYED,

		FIRST_PLAYED_ENHANCED,
		LAST_PLAYED_ENHANCED,
		ADDED_ENHANCED,

		ARTIST_LAST_PLAYED,

		MAX_NUM_FIELDS	// always last entry in this enum
	};

	static std::vector<t_filetimestamp> playtimes_get(metadb_index_hash hash, bool last_fm_times) {
		std::vector<t_filetimestamp> playTimes;
		record_t record = getRecord(hash);

		if (last_fm_times) {
			return record.lastfmPlaytimes;
		}
		else {
			return record.foobarPlaytimes;
		}

		return playTimes;
	}

	static t_filetimestamp artisttime_get(metadb_index_hash hash) {

	}

	static int playcount_get(metadb_index_hash hash, bool last_fm_times) {
		record_t record = getRecord(hash);
		int count = record.numLastfmPlays;

		if (config.IncrementLastfmWithPlaycount && record.numFoobarPlays && record.numLastfmPlays) {
			t_filetimestamp lastRecordedTime;
			if (record.numLastfmPlays) {
				std::vector<t_filetimestamp>::reverse_iterator it = record.lastfmPlaytimes.rbegin();
				lastRecordedTime = *it;
			}

			std::vector<t_filetimestamp>::reverse_iterator rit = record.foobarPlaytimes.rbegin();
			if (*rit > lastRecordedTime + 300 * filetimestamp_1second_increment) { // > lastRecordedTime + 5mins
				count++;
			}
		}
		return count;
	}

	titleformat_object::ptr first_played_script;
	titleformat_object::ptr last_played_script;
	titleformat_object::ptr date_added_script;

	class metadb_display_field_provider_impl : public metadb_display_field_provider {
	public:
		t_uint32 get_field_count() {
			return MAX_NUM_FIELDS;
		}
		void get_field_name(t_uint32 index, pfc::string_base& out) {
			PFC_ASSERT(index >= 0 && index < MAX_NUM_FIELDS);
			switch (index) {
			case PLAYED_TIMES:
				out = "played_times";
				break;
			case PLAYED_TIMES_JS:
				out = "played_times_js";
				break;
			case PLAYED_TIMES_RAW:
				out = "played_times_raw";
				break;
			case LASTFM_PLAYED_TIMES:
				out = "lastfm_played_times";
				break;
			case LASTFM_PLAYED_TIMES_JS:
				out = "lastfm_played_times_js";
				break;
			case LASTFM_PLAY_COUNT:
				out = "lastfm_play_count";
				break;
			case LASTFM_ADDED:
				out = "lastfm_added";
				break;
			case LASTFM_FIRST_PLAYED:
				out = "lastfm_first_played";
				break;
			case LASTFM_LAST_PLAYED:
				out = "lastfm_last_played";
				break;
			case FIRST_PLAYED_ENHANCED:
				out = "first_played_enhanced";
				break;
			case LAST_PLAYED_ENHANCED:
				out = "last_played_enhanced";
				break;
			case ADDED_ENHANCED:
				out = "added_enhanced";
				break;
			case ARTIST_LAST_PLAYED:
				out = "artist_last_played";
				break;
			}
		}
		bool process_field(t_uint32 index, metadb_handle* handle, titleformat_text_out* out) {
			PFC_ASSERT(index >= 0 && index < MAX_NUM_FIELDS);
			metadb_index_hash hash;
			GUID meta_guid = guid_foo_enhanced_playcount_index;
			if (index == ARTIST_LAST_PLAYED) {
				meta_guid = guid_foo_enhanced_playcount_artist_index;
			}
			if (!clientByGUID(meta_guid)->hashHandle(handle, hash)) return false;
			std::vector<t_filetimestamp> playTimes, lastfmPlayTimes;
			t_filetimestamp fbTime = 0, lastfmTime = 0, firstPlayed = 0, lastPlayed = 0;
			file_info_impl info;
			unsigned int count;
			pfc::string_formatter p_out;

			switch (index) {
			case PLAYED_TIMES:
			case PLAYED_TIMES_JS:
			case PLAYED_TIMES_RAW:
				playTimes = playtimes_get(hash, false);
				if (!playTimes.size()) {
					out->write(titleformat_inputtypes::meta, "[]");
				}
				else {
					switch (index) {
					case PLAYED_TIMES:
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, true, false).c_str());
						break;
					case PLAYED_TIMES_JS:
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, false, true).c_str());
						break;
					case PLAYED_TIMES_RAW:
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, false, false).c_str());
						break;
					}
				}
				break;
			case LASTFM_PLAYED_TIMES:
			case LASTFM_PLAYED_TIMES_JS:
				playTimes = playtimes_get(hash, true);
				if (!playTimes.size()) {
					out->write(titleformat_inputtypes::meta, "[]");
				}
				else {
					if (index == LASTFM_PLAYED_TIMES) {
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, true, false).c_str());
					}
					else {
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, false, true).c_str());
					}
				}
				break;
			case LASTFM_PLAY_COUNT:
				count = playcount_get(hash, true);
				out->write_int(titleformat_inputtypes::meta, count);
				if (count == 0) {
					return false;
				}
				break;
			case LASTFM_ADDED:
			case LASTFM_FIRST_PLAYED:
				playTimes = playtimes_get(hash, true);
				if (!playTimes.size()) {
					out->write(titleformat_inputtypes::meta, "N/A");
					return false;
				}
				else {
					out->write(titleformat_inputtypes::meta,
						foobar2000_io::format_filetimestamp(playTimes.front()));
				}
				break;
			case LASTFM_LAST_PLAYED:
				playTimes = playtimes_get(hash, true);
				if (!playTimes.size()) {
					out->write(titleformat_inputtypes::meta, "N/A");
					return false;
				}
				else {
					out->write(titleformat_inputtypes::meta,
						foobar2000_io::format_filetimestamp(playTimes.back()));
				}
				break;
			case FIRST_PLAYED_ENHANCED:
				playTimes = playtimes_get(hash, false);
				lastfmPlayTimes = playtimes_get(hash, true);
				fbTime = lastfmTime = kNoDate;
				if (playTimes.size()) {
					fbTime = playTimes.front();
				}
				if (lastfmPlayTimes.size()) {
					lastfmTime = lastfmPlayTimes.front();
				}
				if (fbTime < lastfmTime) {
					firstPlayed = fbTime;
				}
				else {
					firstPlayed = lastfmTime;
				}
				if (firstPlayed != kNoDate) {
					out->write(titleformat_inputtypes::meta,
						foobar2000_io::format_filetimestamp(firstPlayed));
				}
				else {
					if (first_played_script.is_empty()) {
						static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(first_played_script, "%first_played%");
					}
					handle->format_title(NULL, p_out, first_played_script, NULL);

					if (strcmp(p_out.toString(), "N/A")) {
						t_filetimestamp first_played = foobar2000_io::filetimestamp_from_string(p_out);
						out->write(titleformat_inputtypes::meta,
							foobar2000_io::format_filetimestamp(first_played));
					}
					else {
						out->write(titleformat_inputtypes::meta, "N/A");
						return false;
					}
				}
				break;
			case LAST_PLAYED_ENHANCED:
				playTimes = playtimes_get(hash, false);
				lastfmPlayTimes = playtimes_get(hash, true);
				if (playTimes.size()) {
					fbTime = playTimes.back();
				}
				if (lastfmPlayTimes.size()) {
					lastfmTime = lastfmPlayTimes.back();
				}
				if (fbTime > lastfmTime) {
					lastPlayed = fbTime;
				}
				else {
					lastPlayed = lastfmTime;
				}
				if (lastPlayed) {
					out->write(titleformat_inputtypes::meta,
						foobar2000_io::format_filetimestamp(lastPlayed));
				}
				else {
					if (last_played_script.is_empty()) {
						static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(last_played_script, "%last_played%");
					}
					handle->format_title(NULL, p_out, last_played_script, NULL);

					if (strcmp(p_out.toString(), "N/A")) {
						t_filetimestamp last_played = foobar2000_io::filetimestamp_from_string(p_out);
						out->write(titleformat_inputtypes::meta,
							foobar2000_io::format_filetimestamp(last_played));
					}
					else {
						out->write(titleformat_inputtypes::meta, "N/A");
						return false;
					}
				}
				break;
			case ADDED_ENHANCED:
				if (date_added_script.is_empty()) {
					static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(date_added_script, "%added%");
				}
				
				handle->format_title(NULL, p_out, date_added_script, NULL);

				if (strcmp(p_out.toString(), "N/A")) {
					t_filetimestamp added = foobar2000_io::filetimestamp_from_string(p_out);

					lastfmPlayTimes = playtimes_get(hash, true);
					if (lastfmPlayTimes.size() && lastfmPlayTimes.front() < added) {
						out->write(titleformat_inputtypes::meta,
							foobar2000_io::format_filetimestamp(lastfmPlayTimes.front()));
					}
					else {
						out->write(titleformat_inputtypes::meta,
							foobar2000_io::format_filetimestamp(added));
					}
				}
				else {
					// if playing a song that is not in media library %added% returns "N/A" so we should too
					out->write(titleformat_inputtypes::meta, "N/A");
					return false;
				}
				break;
			case ARTIST_LAST_PLAYED:
				artist_record_t rec = getArtistRecord(hash);
				if (rec.artistLastPlayed == kNoDate) {
					out->write(titleformat_inputtypes::meta, "N/A");
					return false;
				} else {
					out->write(titleformat_inputtypes::meta,
						foobar2000_io::format_filetimestamp(rec.artistLastPlayed));
				}
				break;
			}
			return true;
		}
	};

	static service_factory_single_t<metadb_display_field_provider_impl> g_metadb_display_field_provider_impl;

	std::string t_uint64_to_string(t_uint64 value, bool jsTimestamp) {
		std::ostringstream os;
		if (jsTimestamp) {
			t_uint64 jsValue = util::timestampWindowsToJS(value);	// convert to unix timestamp, then add milliseconds for JS
			os << jsValue;
		}
		else {
			os << value;
		}
		return os.str();
	}

	static std::string getPlayTimesStr(std::vector<t_filetimestamp> playTimes, bool convertTimeStamp, bool jsTimeStamp, bool noArrayChars) {
		std::string str;
		size_t index = 0;

		if (!noArrayChars) {
			str += "[";
		}
		for (std::vector<t_filetimestamp>::iterator it = playTimes.begin(); it != playTimes.end(); ++it, ++index) {
			if (*it != 0) {
				if (convertTimeStamp) {
					if (!noArrayChars) {
						str += "\"";
					}
					str.append(foobar2000_io::format_filetimestamp(*it));
					if (!noArrayChars) {
						str += "\"";
					}
				}
				else {
					str += t_uint64_to_string(*it, jsTimeStamp);
				}
				if (index + 1 < playTimes.size()) {
					str.append(", ");
				}
			}
		}
		if (!noArrayChars) {
			str += "]";
		}
		return str;
	}

	static const char strPropertiesGroup[] = "Enhanced Playback Statistics (Last.fm scrobble data)";

	// This class provides our information for the properties dialog
	class track_property_provider_impl : public track_property_provider_v2 {
	public:
		void workThisIndex(GUID const& whichID, double priorityBase, metadb_handle_list_cref p_tracks, track_property_callback& p_out) {
			auto client = clientByGUID(whichID);
			pfc::avltree_t<metadb_index_hash> hashes;
			const size_t trackCount = p_tracks.get_count();
			for (size_t trackWalk = 0; trackWalk < trackCount; ++trackWalk) {
				metadb_index_hash hash;
				if (client->hashHandle(p_tracks[trackWalk], hash)) {
					hashes += hash;
				}
			}

			uint64_t lastfm_playcount = 0;
			pfc::string8 first_scrobble_str = "", last_scrobble_str, scrobble_times;
			{
				size_t count = 0;
				bool bFirst = true;
				bool bVarComments = false;
				record_t first_record;
				t_filetimestamp first_scrobble = 0, last_scrobble = kNoDate;
				for (auto i = hashes.first(); i.is_valid(); ++i) {
					record_t rec = getRecord(*i);
					if (rec.numLastfmPlays) {
						lastfm_playcount += rec.numLastfmPlays;

						count++;

						if (bFirst) {
							first_record = rec;
							first_scrobble = rec.lastfmPlaytimes.front();
							last_scrobble = rec.lastfmPlaytimes.back();
						}
						else {
							if (rec.lastfmPlaytimes.front() < first_scrobble) {
								first_scrobble = rec.lastfmPlaytimes.front();
							}
							if (rec.lastfmPlaytimes.front() > last_scrobble) {
								last_scrobble = rec.lastfmPlaytimes.back();
							}
						}

						bFirst = false;
					}
				}


				if (count == 1) {
					if (first_record.numLastfmPlays > 0) {
						scrobble_times = getPlayTimesStr(first_record.lastfmPlaytimes, true, false, true).c_str();
#define MAX_PROPERTY_LENGTH 500
						if (scrobble_times.get_length() > MAX_PROPERTY_LENGTH) {
							scrobble_times.truncate(MAX_PROPERTY_LENGTH);
							scrobble_times += "...";
						}
					}
					else {
						scrobble_times = "N/A";
					}
				}
				if (first_scrobble != 0) {
					first_scrobble_str = foobar2000_io::format_filetimestamp(first_scrobble);
					last_scrobble_str = foobar2000_io::format_filetimestamp(last_scrobble);
				}
			}

			p_out.set_property(strPropertiesGroup, priorityBase + 0, PFC_string_formatter() << "Scrobbled", PFC_string_formatter() << lastfm_playcount << " times");
			p_out.set_property(strPropertiesGroup, priorityBase + 1, PFC_string_formatter() << "First scrobble", first_scrobble_str);
			p_out.set_property(strPropertiesGroup, priorityBase + 2, PFC_string_formatter() << "Last scrobble", last_scrobble_str);
			if (scrobble_times.length() > 0) {
				p_out.set_property(strPropertiesGroup, priorityBase + 3, PFC_string_formatter() << "Last.fm scrobbles", scrobble_times);
			}
		}
		void enumerate_properties(metadb_handle_list_cref p_tracks, track_property_callback& p_out) {
			workThisIndex(guid_foo_enhanced_playcount_index, 0, p_tracks, p_out);
		}
		void enumerate_properties_v2(metadb_handle_list_cref p_tracks, track_property_callback_v2& p_out) {
			if (p_out.is_group_wanted(strPropertiesGroup)) {
				enumerate_properties(p_tracks, p_out);
			}
		}

		bool is_our_tech_info(const char* p_name) {
			// If we do stuff with tech infos read from the file itself (see file_info::info_* methods), signal whether this field belongs to us
			// We don't do any of this, hence false
			return false;
		}

	};


	static service_factory_single_t<track_property_provider_impl> g_track_property_provider_impl;
}