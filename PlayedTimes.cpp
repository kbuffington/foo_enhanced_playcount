#include "stdafx.h"
#include "lastfm.h"
#include <vector>
#include <sstream>
#include "util.h"
#include "globals.h"
#include "PlaycountConfig.h"

/*
========================================================================
   Sample implementation of metadb_index_client and a rating database.
========================================================================

The rating data is all maintained by metadb backend, we only present and alter it when asked to.
Relevant classes:
metadb_index_client_impl - turns track info into a database key to which our data gets pinned.
init_stage_callback_impl - initializes ourselves at the proper moment of the app lifetime.
metadb_display_field_provider_impl - publishes %foo_sample_rating% via title formatting.
*/

using namespace foo_enhanced_playcount;
using namespace pfc;

namespace {

	std::string t_uint64_to_string(t_uint64 value, bool jsTimestamp);

	PlaycountConfig const& config{ Config };

	// Replace with your own when reusing.
	// Always recreate guid_foo_enhanced_playcount_index if your metadb_index_client_impl hashing semantics changed or else you run into inconsistent/nonsensical data.
	static const GUID guid_foo_enhanced_playcount_index = { 0xc1bd000, 0x43e7, 0x4078, { 0xb8, 0x85, 0x48, 0xee, 0x42, 0x49, 0xde, 0xc2 } };

	// Pattern by which we pin our data to.
	// If multiple songs in the library evaluate to the same string,
	// they will be considered the same by our component,
	// and data applied to one will also show up with the rest.
	static const char strPinTo[] = "%artist% %album% $if2(%discnumber%,1) %tracknumber% %title%";

	// Retain pinned data for four weeks if there are no matching items in library
	static const t_filetimestamp retentionPeriod = system_time_periods::week * 4;

	// Returns a titleformat_object used to generate key values for our database.
	static titleformat_object::ptr makeKeyObj(const char * pinTo) {
		titleformat_object::ptr obj;
		titleformat_compiler::get()->compile_force(obj, pinTo);
		return obj;
	}

	// A class that turns metadata + location info into hashes to which our data gets pinned by the backend.
	class metadb_index_client_impl : public metadb_index_client {
	public:
		metadb_index_hash transform(const file_info & info, const playable_location & location) {
			static auto obj = makeKeyObj(strPinTo); // initialized first time we get here and reused later
			static hasher_md5::ptr hasher = hasher_md5::get();
			pfc::string_formatter str;
			obj->run_simple(location, &info, str);
			// Make MD5 hash of the string, then reduce it to 64-bit metadb_index_hash
			return hasher->process_single_string(str).xorHalve();
		}
	};

	// Static instance, never destroyed (dies with the process)
	// Uses service_impl_single_t, reference counting disabled
	static metadb_index_client_impl * g_client = new service_impl_single_t<metadb_index_client_impl>;

	// Static cached ptr to metadb_index_manager
	// Cached because we'll be calling it a lot on per-track basis, let's not pass it everywhere to low level functions
	static metadb_index_manager::ptr g_api;

	// An init_stage_callback to hook ourselves into the metadb
	// We need to do this properly early to prevent dispatch_global_refresh() from new fields that we added from hammering playlists etc
	class init_stage_callback_impl : public init_stage_callback {
	public:
		void on_init_stage(t_uint32 stage) {
			if (stage == init_stages::before_config_read) {
				// Important, handle the exceptions here!
				// This will fail if the files holding our data are somehow corrupted.
				try {
					g_api = metadb_index_manager::get();
					g_api->add(g_client, guid_foo_enhanced_playcount_index, retentionPeriod);
				} catch (std::exception const & e) {
					g_api->remove(guid_foo_enhanced_playcount_index);
					FB2K_console_formatter() << "[foo_enhanced_playcount] Critical initialization failure: " << e;
					return;
				}
				g_api->dispatch_global_refresh();
			}
		}
	};
	class initquit_impl : public initquit {
	public:
		void on_quit() {
			// Cleanly kill g_cachedAPI before reaching static object destructors or else
			g_api.release();
		}
	};
	static service_factory_single_t<init_stage_callback_impl> g_init_stage_callback_impl;
	static service_factory_single_t<initquit_impl> g_initquit_impl;

#define kCurrVersion   1

	struct record_t {
		unsigned int version = kCurrVersion;
		unsigned int numFoobarPlays = 0;
		unsigned int numLastfmPlays = 0;
		int unused = 0;			// available for later
		std::vector<t_filetimestamp> foobarPlaytimes;
		std::vector<t_filetimestamp> lastfmPlaytimes;
	};

	void copyTimestampsToVector(t_filetimestamp *buf, const size_t numElements, std::vector<t_filetimestamp>& v) {
		v.insert(v.begin(), buf, buf + numElements);
	}

	static record_t getRecord(metadb_index_hash hash) {
		unsigned int buf[10004];
		record_t record;
		int size = g_api->get_user_data_here(guid_foo_enhanced_playcount_index, hash, &buf, sizeof(buf));
		if (!size) {
			return record;
		}
		int numElements;

		if (buf[0] > 0 && buf[0] < 9) {
			record.version = buf[0];
		} else {
			record.version = 0;
		}

		switch (record.version) {
			case 0:
				numElements = size / sizeof(t_filetimestamp);
				record.numFoobarPlays = numElements;
				copyTimestampsToVector((t_filetimestamp *)buf, record.numFoobarPlays, record.foobarPlaytimes);
				record.numLastfmPlays = 0;
				break;
			case 1:
				record.numFoobarPlays = buf[1];
				record.numLastfmPlays = buf[2];
				if (record.numFoobarPlays > 0)
					copyTimestampsToVector((t_filetimestamp *)&buf[4], record.numFoobarPlays, record.foobarPlaytimes);
				if (record.numLastfmPlays > 0)
					copyTimestampsToVector((t_filetimestamp *)&buf[4] + record.numFoobarPlays, 
							record.numLastfmPlays, record.lastfmPlaytimes);
				break;
		}

		return record;
	}

	static void setRecord(metadb_index_hash hash, record_t record) {
		unsigned int buf[10004];
		size_t size = 0;
		memcpy(buf, &record, 4 * sizeof(int));
		size += 4;
		if (record.foobarPlaytimes.size()) {
			memcpy(buf + size, &record.foobarPlaytimes[0], record.foobarPlaytimes.size() * sizeof t_filetimestamp);
			size += record.foobarPlaytimes.size() * sizeof t_filetimestamp / sizeof(int);
		}

		if (record.lastfmPlaytimes.size()) {
			memcpy(buf + size, &record.lastfmPlaytimes[0], record.lastfmPlaytimes.size() * sizeof t_filetimestamp);
			size += record.lastfmPlaytimes.size() * sizeof t_filetimestamp / sizeof(int);
		}

		g_api->set_user_data(guid_foo_enhanced_playcount_index, hash, buf, size * sizeof(int));
	}

	std::vector<t_filetimestamp> getLastFmPlaytimes(metadb_handle_ptr p_item, const t_filetimestamp lastPlay) {
		std::vector<t_filetimestamp> playTimes;
		file_info_impl info;
		if (config.EnableLastfmPlaycounts && p_item->get_info(info)) {
			if (info.get_length() > 29) {	// you can't scrobble a song less than 30 seconds long, so don't check to see if it was scrobbled.
				pfc::string8 time;
#ifdef DEBUG
				t_filetimestamp start = filetimestamp_from_system_timer();
#endif
				pfc::string8 artist;
				pfc::string8 album;
				pfc::string8 title;
				artist = info.meta_get("ARTIST", 0);
				album = info.meta_get("ALBUM", 0);
				title = info.meta_get("TITLE", 0);

				Lastfm *lfm = new Lastfm(artist, album, title);
				playTimes = lfm->queryLastfm(lastPlay);
#ifdef DEBUG
				t_filetimestamp end = filetimestamp_from_system_timer();
				time << "Time Elapsed: " << (end - start) / 10000 << "ms - ";
#endif
				FB2K_console_formatter() << time << "Found " << playTimes.size() << " plays in last.fm (since last recorded scrobble) of " << title;
			}
		}

		return playTimes;
	}

	static std::vector<t_filetimestamp> playtimes_get(metadb_index_hash hash, bool last_fm_times) {
		std::vector<t_filetimestamp> playTimes;
		record_t record = getRecord(hash);

		if (last_fm_times) {
			return record.lastfmPlaytimes;
		} else {
			return record.foobarPlaytimes;
		}

		return playTimes;
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
			if (*rit > lastRecordedTime + 3000000000) {	// lastRecordedTime + 5mins
				count++;
			}
		}
		return count;
	}

	titleformat_object::ptr playback_statistics_script;

	static void playtime_set(metadb_index_hash hash, record_t record, t_filetimestamp fp, t_filetimestamp lp) {
		t_filetimestamp time = filetimestamp_from_system_timer();
		time /= 10000000;
		time *= 10000000;
		record.version = kCurrVersion;

		if (record.numFoobarPlays == 0 && fp) {	// add first played and last played if this is the first time we've recorded a play for this file
			record.foobarPlaytimes.push_back(fp);
			if (fp != lp) {
				record.foobarPlaytimes.push_back(lp);
			}
		}
		record.foobarPlaytimes.push_back(time);
		record.numFoobarPlays = record.foobarPlaytimes.size();
		record.numLastfmPlays = record.lastfmPlaytimes.size();

		setRecord(hash, record);
	}

	class my_playback_statistics_collector : public playback_statistics_collector {
	public:
		void on_item_played(metadb_handle_ptr p_item) {
			if (playback_statistics_script.is_empty()) {
				static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(playback_statistics_script, "%first_played%~%last_played%");
			}
			pfc::string_formatter p_out;

			metadb_index_hash hash;
			g_client->hashHandle(p_item, hash);

			p_item->format_title(NULL, p_out, playback_statistics_script, NULL);
			t_size divider = p_out.find_first('~');
			char firstPlayed[25], lastPlayed[25];
			strncpy_s(firstPlayed, p_out.toString(), divider);
			strcpy_s(lastPlayed, p_out.toString() + divider + 1);
			t_filetimestamp fp = 0, lp = 0;

			if (strcmp(firstPlayed, "N/A")) {
				fp = foobar2000_io::filetimestamp_from_string(firstPlayed);
				lp = foobar2000_io::filetimestamp_from_string(lastPlayed);
			}

			record_t record = getRecord(hash);
			std::vector<t_filetimestamp> playTimes;
			playTimes = getLastFmPlaytimes(p_item, record.lastfmPlaytimes.size() ? record.lastfmPlaytimes.back() : 0);

			record.lastfmPlaytimes.insert(record.lastfmPlaytimes.end(), playTimes.begin(), playTimes.end());

			playtime_set(hash, record, fp, lp);
		}
	};

	static playback_statistics_collector_factory_t<my_playback_statistics_collector> g_my_stat_collector;

	pfc::string8 meta_get_if_exists(const file_info * info, const char* key, const char* default) {
		if (info->meta_exists(key)) {
			return info->meta_get(key, 0);
		} else {
			return default;
		}
	}

	class metadb_io_edit_callback_impl : public metadb_io_edit_callback {
	public:
		typedef const pfc::list_base_const_t<const file_info*> & t_infosref;
		void on_edited(metadb_handle_list_cref items, t_infosref before, t_infosref after) {
			for (size_t t = 0; t < items.get_count(); t++) {
				metadb_index_hash hashOld, hashNew;
				static hasher_md5::ptr hasher = hasher_md5::get();

				g_client->hashHandle(items[t], hashOld);

				pfc::string8 artist = meta_get_if_exists(after[t], "ARTIST", "");
				pfc::string8 album = meta_get_if_exists(after[t], "ALBUM", "");
				pfc::string8 discnum = meta_get_if_exists(after[t], "DISCNUMBER", "1");
				pfc::string8 tracknum = meta_get_if_exists(after[t], "TRACKNUMBER", "");
				pfc::string8 title = meta_get_if_exists(after[t], "TITLE", "");

				pfc::string_formatter strAfter;
				strAfter << artist << " " << album << " " << discnum << " " << tracknum << " " << title;

				hashNew = hasher->process_single_string(strAfter).xorHalve();
				if (hashOld != hashNew) {
					record_t record = getRecord(hashOld);
					if (record.numFoobarPlays || record.numLastfmPlays) {
						record_t newRecord = getRecord(hashNew);
						if (newRecord.numFoobarPlays <= record.numFoobarPlays &&
							newRecord.numLastfmPlays <= record.numLastfmPlays &&
							(record.numFoobarPlays || record.numLastfmPlays)) {
							setRecord(hashNew, record);
#ifdef DEBUG
							FB2K_console_formatter() << COMPONENT_NAME": moved record";
#endif
						}
					}
				}
			}
		}
	};

	static service_factory_single_t<metadb_io_edit_callback_impl> g_my_metadb_io;


	std::string t_uint64_to_string(t_uint64 value, bool jsTimestamp) {
		std::ostringstream os;
		if (jsTimestamp) {
			t_uint64 jsValue = util::timestampWindowsToJS(value);	// convert to unix timestamp, then add milliseconds for JS
			os << jsValue;
		} else {
			os << value;
		}
		return os.str();
	}

	static std::string getPlayTimesStr(std::vector<t_filetimestamp> playTimes, bool convertTimeStamp, bool jsTimeStamp) {
		std::string str;
		size_t index = 0;

		str += "[";
		for (std::vector<t_filetimestamp>::iterator it = playTimes.begin(); it != playTimes.end(); ++it, ++index) {
			if (convertTimeStamp) {
				str += "\"";
				str.append(format_filetimestamp::format_filetimestamp(*it));
				str += "\"";
			} else {
				str += t_uint64_to_string(*it, jsTimeStamp);
			}
			if (index + 1 < playTimes.size()) {
				str.append(", ");
			}
		}
		str += "]";
		return str;
	}

	enum provided_fields {
		PLAYED_TIMES,
		PLAYED_TIMES_JS,
		PLAYED_TIMES_RAW,

		LASTFM_PLAYED_TIMES,
		LASTFM_PLAYED_TIMES_JS,
		LASTFM_PLAY_COUNT,

		MAX_NUM_FIELDS	// always last entry in this enum
	};

	// Provider of the %foo_sample_rating% field
	class metadb_display_field_provider_impl : public metadb_display_field_provider {
	public:
		t_uint32 get_field_count() {
			return MAX_NUM_FIELDS;
		}
		void get_field_name(t_uint32 index, pfc::string_base & out) {
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
			}
		}
		bool process_field(t_uint32 index, metadb_handle * handle, titleformat_text_out * out) {
			PFC_ASSERT(index >= 0 && index < MAX_NUM_FIELDS);
			metadb_index_hash hash;
			if (!g_client->hashHandle(handle, hash)) return false;
			std::vector<t_filetimestamp> playTimes;
			file_info_impl info;

			switch (index) {
				case PLAYED_TIMES:
				case PLAYED_TIMES_JS:
				case PLAYED_TIMES_RAW:
					playTimes = playtimes_get(hash, false);
					if (!playTimes.size()) {
						out->write(titleformat_inputtypes::meta, "[]");
					} else {
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
					} else {
						if (index == LASTFM_PLAYED_TIMES) {
							out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, true, false).c_str());
						} else {
							out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, false, true).c_str());
						}
					}
					break;
				case LASTFM_PLAY_COUNT:
					auto count = playcount_get(hash, true);
					out->write_int(titleformat_inputtypes::meta, count);
					break;
			}
			return true;
		}
	};

	static service_factory_single_t<metadb_display_field_provider_impl> g_metadb_display_field_provider_impl;
}

// Context Menu functions start here
void ClearLastFmRecords(metadb_handle_list_cref items) {
	try {
		if (items.get_count() == 0) throw pfc::exception_invalid_params();
		
		for (size_t t = 0; t < items.get_count(); t++) {
			metadb_index_hash hash;
			g_client->hashHandle(items[t], hash);

			record_t record = getRecord(hash);
			record.lastfmPlaytimes.clear();
			record.numLastfmPlays = 0;
			setRecord(hash, record);
			g_api->dispatch_refresh(guid_foo_enhanced_playcount_index, hash);
		}

	}
	catch (std::exception const & e) {
		popup_message::g_complain("Could not remove last.fm plays", e);
	}
}

void GetLastfmScrobbles(metadb_handle_list_cref items) {
	try {
		if (items.get_count() == 0 || items.get_count() > 50) throw pfc::exception_invalid_params();
		for (size_t t = 0; t < items.get_count(); t++) {
			metadb_index_hash hash;
			g_client->hashHandle(items[t], hash);

			record_t record = getRecord(hash);
			std::vector<t_filetimestamp> playTimes;
			playTimes = getLastFmPlaytimes(items[t], record.lastfmPlaytimes.size() ? record.lastfmPlaytimes.back() : 0);
			record.lastfmPlaytimes.insert(record.lastfmPlaytimes.end(), playTimes.begin(), playTimes.end());
			record.numLastfmPlays = record.lastfmPlaytimes.size();

			setRecord(hash, record);
			g_api->dispatch_refresh(guid_foo_enhanced_playcount_index, hash);
		}
	}
	catch (std::exception const & e) {
		if (items.get_count() > 50) {
			popup_message::g_complain("Can only retrieve scrobbles of 50 tracks at a time.", e);
		}
		else {
			popup_message::g_complain("Could not retrieve last.fm scrobbles", e);
		}
	}
}