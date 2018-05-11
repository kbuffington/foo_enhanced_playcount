#include "stdafx.h"
#include "lastfm.h"
#include <vector>
#include <sstream>
#include "util.h"
#include "globals.h"
#include "PlaycountConfig.h"
#include "PlayedTimes.h"


using namespace foo_enhanced_playcount;
using namespace pfc;
using namespace enhanced_playcount;

void GetLastfmScrobblesThreaded(metadb_handle_list_cref items);

namespace enhanced_playcount {

	std::string t_uint64_to_string(t_uint64 value, bool jsTimestamp);

	PlaycountConfig const& config{ Config };

	// Pattern by which we pin our data to.
	// If multiple songs in the library evaluate to the same string,
	// they will be considered the same by our component,
	// and data applied to one will also show up with the rest.
	static const char strObsoletePinTo[] = "%artist% %album% $if2(%discnumber%,1) %tracknumber% %title%";
	static const char strPinTo[] = "%artist% - $year($if2(%date%,%original release date%)) - %album% $if2(%discnumber%,1)-%tracknumber% %title%";

	// Retain pinned data for four weeks if there are no matching items in library
	static const t_filetimestamp retentionPeriod = system_time_periods::week * 4;

	void mb_to_lower(const char * src, pfc::string_base & dest) {
		for (;;) {
			unsigned c1;
			t_size d1;
			d1 = utf8_decode_char(src, c1);
			if (d1 == 0) break;
			else {
				dest.add_char(charLower(c1));
			}
			src += d1;
		}
	}

	// A class that turns metadata + location info into hashes to which our data gets pinned by the backend.
	class metadb_index_client_impl : public metadb_index_client {
	public:
		metadb_index_client_impl(const char * pinTo, bool toLower = false) {
			static_api_ptr_t<titleformat_compiler>()->compile_force(m_keyObj, pinTo);
			forceLowercase = toLower;
		}

		metadb_index_hash transform(const file_info & info, const playable_location & location) {
			pfc::string_formatter str, str_lower;
			pfc::string_formatter *strPtr = &str;
			m_keyObj->run_simple(location, &info, str);
			if (forceLowercase) {
				mb_to_lower(str, str_lower);
				strPtr = &str_lower;
			}
			// Make MD5 hash of the string, then reduce it to 64-bit metadb_index_hash
			return static_api_ptr_t<hasher_md5>()->process_single_string(*strPtr).xorHalve();
		}
	private:
		bool forceLowercase;
		titleformat_object::ptr m_keyObj;
	};

	static metadb_index_client_impl * clientByGUID(const GUID & guid) {
		// Static instances, never destroyed (deallocated with the process), created first time we get here
		// Using service_impl_single_t, reference counting disabled
		// This is somewhat ugly, operating on raw pointers instead of service_ptr, but OK for this purpose
		static metadb_index_client_impl * g_clientIndex = new service_impl_single_t<metadb_index_client_impl>(strPinTo, true);
		static metadb_index_client_impl * g_clientObsolete = new service_impl_single_t<metadb_index_client_impl>(strObsoletePinTo);

		PFC_ASSERT(guid == guid_foo_enhanced_playcount_index || guid == guid_foo_enhanced_playcount_obsolete);
		return (guid == guid_foo_enhanced_playcount_index) ? g_clientIndex : g_clientObsolete;
	}

	// Static cached ptr to metadb_index_manager
	// Cached because we'll be calling it a lot on per-track basis, let's not pass it everywhere to low level functions
	// Obtaining the pointer from core is reasonably efficient - log(n) to the number of known service classes, but not good enough for something potentially called hundreds of times
	static metadb_index_manager::ptr g_cachedAPI;
	static metadb_index_manager::ptr theAPI() {
		auto ret = g_cachedAPI;
		if (ret.is_empty()) ret = metadb_index_manager::get(); // since fb2k SDK v1.4, core API interfaces have a static get() method
		return ret;
	}

	static bool dbNeedsConversion = false;

	// An init_stage_callback to hook ourselves into the metadb
	// We need to do this properly early to prevent dispatch_global_refresh() from new fields that we added from hammering playlists etc
	class init_stage_callback_impl : public init_stage_callback {
	public:
		void on_init_stage(t_uint32 stage) {
			if (stage == init_stages::before_config_read) {
				auto api = metadb_index_manager::get();
				g_cachedAPI = api;
				// Important, handle the exceptions here!
				// This will fail if the files holding our data are somehow corrupted.
				try {
					api = metadb_index_manager::get();
					if (api->have_orphaned_data(guid_foo_enhanced_playcount_obsolete)) {
						dbNeedsConversion = true;
						FB2K_console_formatter() << COMPONENT_NAME": Found old index-db. Will convert hashes.";
						api->add(clientByGUID(guid_foo_enhanced_playcount_obsolete), guid_foo_enhanced_playcount_obsolete, retentionPeriod);
					}
					api->add(clientByGUID(guid_foo_enhanced_playcount_index), guid_foo_enhanced_playcount_index, retentionPeriod);
				} catch (std::exception const & e) {
					api->remove(guid_foo_enhanced_playcount_index);
					FB2K_console_formatter() << COMPONENT_NAME": Critical initialization failure: " << e;
					return;
				}
				api->dispatch_global_refresh();
			}
		}
	};
	class initquit_impl : public initquit {
	public:
		void on_quit() {
			// Cleanly kill g_cachedAPI before reaching static object destructors or else
			g_cachedAPI.release();
		}
	};
	static service_factory_single_t<init_stage_callback_impl> g_init_stage_callback_impl;
	static service_factory_single_t<initquit_impl> g_initquit_impl;

	void copyTimestampsToVector(t_filetimestamp *buf, const size_t numElements, std::vector<t_filetimestamp>& v) {
		v.insert(v.begin(), buf, buf + numElements);
	}

	void convertHashes(void) 
	{
		pfc::list_t<metadb_index_hash> hashes;
#if 1
		if (dbNeedsConversion) {
			theAPI()->get_all_hashes(guid_foo_enhanced_playcount_obsolete, hashes);
			int count = 0;
			for (size_t hashWalk = 0; hashWalk < hashes.get_count(); ++hashWalk) {
				auto hash = hashes[hashWalk];
				metadb_handle_list tracks;
				theAPI()->get_ML_handles(guid_foo_enhanced_playcount_obsolete, hash, tracks);
				if (tracks.get_count() > 0) {
					record_t record = getRecord(hash, guid_foo_enhanced_playcount_obsolete);
					if (record.numFoobarPlays > 0 || record.numLastfmPlays > 0) {
						count++;
						metadb_index_hash hash_new;
						clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(tracks[0], hash_new);
						setRecord(hash_new, record, guid_foo_enhanced_playcount_index);
					}
				}
			}
			theAPI()->remove(guid_foo_enhanced_playcount_obsolete);
			theAPI()->erase_orphaned_data(guid_foo_enhanced_playcount_obsolete);
			theAPI()->save_index_data(guid_foo_enhanced_playcount_index);
			FB2K_console_formatter() << COMPONENT_NAME": Converted " << count << " records. Deleted old database.";
		}
#endif
	}

	record_t getRecord(metadb_index_hash hash, const GUID index_guid) {
		unsigned int buf[10004];
		record_t record;
		int size = 0;
		if (g_cachedAPI != NULL) size = g_cachedAPI->get_user_data_here(index_guid, hash, &buf, sizeof(buf));
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

	static void setRecord(metadb_index_hash hash, record_t record, const GUID index_guid) {
		unsigned int buf[10004];
		size_t size = 0;
		record.version = kCurrVersion;
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

		theAPI()->set_user_data(index_guid, hash, buf, size * sizeof(int));
	}

	std::vector<t_filetimestamp> getLastFmPlaytimes(metadb_handle_ptr p_item, metadb_index_hash hash, const t_filetimestamp lastPlay) {
		std::vector<t_filetimestamp> playTimes;
		file_info_impl info;
		if (config.EnableLastfmPlaycounts && p_item->get_info(info)) {
			if (info.meta_exists("ARTIST") && info.meta_exists("TITLE") &&
				(!config.CompareAlbumFields || info.meta_exists("ALBUM")) &&	// compare album fields if required
				info.get_length() > 29) {	// you can't scrobble a song less than 30 seconds long, so don't check to see if it was scrobbled.
				pfc::string8 time;
#ifdef DEBUG
				t_filetimestamp start = filetimestamp_from_system_timer();
#endif
				pfc::string8 artist;
				pfc::string8 title; 
				pfc::string8 album = "";
				
				artist = info.meta_get("ARTIST", 0);
				title = info.meta_get("TITLE", 0);
				if (config.CompareAlbumFields) {
					album = info.meta_get("ALBUM", 0);
				}
				
				Lastfm *lfm = new Lastfm(hash, artist, album, title);
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

	titleformat_object::ptr first_and_last_played_script;
	titleformat_object::ptr date_added_script;
	titleformat_object::ptr first_played_script;
	titleformat_object::ptr last_played_script;

	class my_playback_statistics_collector : public playback_statistics_collector {
	public:
		void on_item_played(metadb_handle_ptr p_item) {
			metadb_index_hash hash;
			clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(p_item, hash);

			record_t record = getRecord(hash);
			t_filetimestamp time = filetimestamp_from_system_timer();
			time /= 10000000;
			time *= 10000000;
			record.foobarPlaytimes.push_back(time);
			record.numFoobarPlays = record.foobarPlaytimes.size();

			setRecord(hash, record);
		}
	};

	static playback_statistics_collector_factory_t<my_playback_statistics_collector> g_my_stat_collector;

	class my_play_callback : public play_callback_static {
	public:
		void on_playback_new_track(metadb_handle_ptr p_track) {
			metadb_index_hash hash;
			clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(p_track, hash);
			t_filetimestamp fp = 0, lp = 0;

			record_t record = getRecord(hash);

			if (config.EnableLastfmPlaycounts) {
				metadb_handle_list p_list;
				p_list.add_item(p_track);
				GetLastfmScrobblesThreaded(p_list);
			}
		}
		void on_playback_starting(play_control::t_track_command p_command, bool p_paused) {}
		void on_playback_stop(play_control::t_stop_reason p_reason) {}
		void on_playback_seek(double p_time) {}
		void on_playback_pause(bool p_state) {}
		void on_playback_edited(metadb_handle_ptr p_track) {}
		void on_playback_dynamic_info(const file_info & p_info) {}
		void on_playback_dynamic_info_track(const file_info & p_info) {}
		void on_playback_time(double p_time) {}
		void on_volume_change(float p_new_val) {}

		/* The play_callback_manager enumerates play_callback_static services and registers them automatically. We only have to provide the flags indicating which callbacks we want. */
		virtual unsigned get_flags() {
			return flag_on_playback_new_track;
		}
	};

	static service_factory_single_t<my_play_callback> g_play_callback_static_factory;

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

				clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(items[t], hashOld);

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
		LASTFM_ADDED,
		LASTFM_FIRST_PLAYED,
		LASTFM_LAST_PLAYED,

		FIRST_PLAYED_ENHANCED,
		LAST_PLAYED_ENHANCED,
		ADDED_ENHANCED,

		MAX_NUM_FIELDS	// always last entry in this enum
	};

#define kNoDate 199999999990000000

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
			}
		}
		bool process_field(t_uint32 index, metadb_handle * handle, titleformat_text_out * out) {
			PFC_ASSERT(index >= 0 && index < MAX_NUM_FIELDS);
			metadb_index_hash hash;
			if (!clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(handle, hash)) return false;
			std::vector<t_filetimestamp> playTimes, lastfmPlayTimes;
			t_filetimestamp fbTime = 0, lastfmTime = 0, firstPlayed = 0, lastPlayed = 0;
			file_info_impl info;
			unsigned int count;

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
					count = playcount_get(hash, true);
					out->write_int(titleformat_inputtypes::meta, count);
					break;
				case LASTFM_ADDED:
				case LASTFM_FIRST_PLAYED:
					playTimes = playtimes_get(hash, true);
					if (!playTimes.size()) {
						out->write(titleformat_inputtypes::meta, "N/A");
						return false;
					} else {
						out->write(titleformat_inputtypes::meta,
							format_filetimestamp::format_filetimestamp(playTimes.front()));
					}
					break;
				case LASTFM_LAST_PLAYED:
					playTimes = playtimes_get(hash, true);
					if (!playTimes.size()) {
						out->write(titleformat_inputtypes::meta, "N/A");
						return false;
					} else {
						out->write(titleformat_inputtypes::meta,
							format_filetimestamp::format_filetimestamp(playTimes.back()));
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
					} else {
						firstPlayed = lastfmTime;
					}
					if (firstPlayed != kNoDate) {
						out->write(titleformat_inputtypes::meta,
							format_filetimestamp::format_filetimestamp(firstPlayed));
					} else {
						if (first_played_script.is_empty()) {
							static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(first_played_script, "%first_played%");
						}
						pfc::string_formatter p_out;
						handle->format_title(NULL, p_out, first_played_script, NULL);

						if (strcmp(p_out.toString(), "N/A")) {
							t_filetimestamp first_played = foobar2000_io::filetimestamp_from_string(p_out);
							out->write(titleformat_inputtypes::meta,
								format_filetimestamp::format_filetimestamp(first_played));
						} else {
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
					} else {
						lastPlayed = lastfmTime;
					}
					if (lastPlayed) {
						out->write(titleformat_inputtypes::meta,
							format_filetimestamp::format_filetimestamp(lastPlayed));
					} else {
						if (last_played_script.is_empty()) {
							static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(last_played_script, "%last_played%");
						}
						pfc::string_formatter p_out;
						handle->format_title(NULL, p_out, last_played_script, NULL);

						if (strcmp(p_out.toString(), "N/A")) {
							t_filetimestamp last_played = foobar2000_io::filetimestamp_from_string(p_out);
							out->write(titleformat_inputtypes::meta,
								format_filetimestamp::format_filetimestamp(last_played));
						} else {
							out->write(titleformat_inputtypes::meta, "N/A");
							return false;
						}
					}
					break;
				case ADDED_ENHANCED:
					if (date_added_script.is_empty()) {
						static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(date_added_script, "%added%");
					}
					pfc::string_formatter p_out;

					handle->format_title(NULL, p_out, date_added_script, NULL);
					
					if (strcmp(p_out.toString(), "N/A")) {
						t_filetimestamp added = foobar2000_io::filetimestamp_from_string(p_out);

						lastfmPlayTimes = playtimes_get(hash, true);
						if (lastfmPlayTimes.size() && lastfmPlayTimes.front() < added) {
							out->write(titleformat_inputtypes::meta,
								format_filetimestamp::format_filetimestamp(lastfmPlayTimes.front()));
						} else {
							out->write(titleformat_inputtypes::meta,
								format_filetimestamp::format_filetimestamp(added));
						}
					} else {
						return false;	// can we get here?
					}
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
			clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(items[t], hash);

			record_t record = getRecord(hash);
			record.lastfmPlaytimes.clear();
			record.numLastfmPlays = 0;
			setRecord(hash, record);
			theAPI()->dispatch_refresh(guid_foo_enhanced_playcount_index, hash);
		}

	}
	catch (std::exception const & e) {
		popup_message::g_complain("Could not remove last.fm plays", e);
	}
}

struct hash_record {
	metadb_index_hash hash;
	metadb_handle_ptr mdb_handle;
	record_t record;
	hash_record(metadb_handle_ptr mdb_ptr) : mdb_handle(mdb_ptr) {}
};

class metadb_refresh_callback : public main_thread_callback {
private:
	metadb_index_hash m_hash;
	record_t m_record;

public:
	metadb_refresh_callback(metadb_index_hash hash, record_t record) : m_hash(hash), m_record(record) {}

	virtual void callback_run()
	{
		if (m_record.numLastfmPlays > 0) {
			setRecord(m_hash, m_record);
			theAPI()->dispatch_refresh(guid_foo_enhanced_playcount_index, m_hash);
		}
	}
};

class get_lastfm_scrobbles : public threaded_process_callback {
public:
	get_lastfm_scrobbles(std::vector<hash_record> items) : m_items(items) {}
	void on_init(HWND p_wnd) {}
	void run(threaded_process_status & p_status, abort_callback & p_abort) {
		try {
			for (size_t t = 0; t < m_items.size(); t++) {
				p_status.set_progress(t, m_items.size());
				p_status.set_item_path(m_items[t].mdb_handle->get_path());

				record_t record = m_items[t].record;
				std::vector<t_filetimestamp> playTimes;
				playTimes = getLastFmPlaytimes(m_items[t].mdb_handle, m_items[t].hash,
					m_items[t].record.lastfmPlaytimes.size() ? m_items[t].record.lastfmPlaytimes.back() : 0);
				record.lastfmPlaytimes.insert(record.lastfmPlaytimes.end(), playTimes.begin(), playTimes.end());
				record.numLastfmPlays = record.lastfmPlaytimes.size();

				if (record.numFoobarPlays == 0) {
					t_filetimestamp fp = 0, lp = 0;
					if (first_and_last_played_script.is_empty()) {
						static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(first_and_last_played_script, "%first_played%~%last_played%");
					}
					pfc::string_formatter p_out;

					m_items[t].mdb_handle->format_title(NULL, p_out, first_and_last_played_script, NULL);
					t_size divider = p_out.find_first('~');
					char firstPlayed[25], lastPlayed[25];
					strncpy_s(firstPlayed, p_out.toString(), divider);
					strcpy_s(lastPlayed, p_out.toString() + divider + 1);

					if (strcmp(firstPlayed, "N/A")) {
						fp = foobar2000_io::filetimestamp_from_string(firstPlayed);
						lp = foobar2000_io::filetimestamp_from_string(lastPlayed);

						record.foobarPlaytimes.push_back(fp);
						if (fp != lp) {
							record.foobarPlaytimes.push_back(lp);
						}
						record.numFoobarPlays = record.foobarPlaytimes.size();
					}
				}

				static_api_ptr_t<main_thread_callback_manager> cm;
				service_ptr_t<metadb_refresh_callback> update_cb =
					new service_impl_t<metadb_refresh_callback>(m_items[t].hash, record);
				cm->add_callback(update_cb);

			}
		} catch (std::exception const & e) {
			m_failMsg = e.what();
		}
	}
	void on_done(HWND p_wnd, bool p_was_aborted) {
		if (!p_was_aborted) {
			if (!m_failMsg.is_empty()) {
				popup_message::g_complain("Could not retrieve last.fm scrobbles", m_failMsg);
			} else {
				// finished succesfully
			}
		}
	}
private:
	pfc::string8 m_failMsg;
	const std::vector<hash_record> m_items;
};

void GetLastfmScrobblesThreaded(metadb_handle_list_cref items) {
	int showProgress = threaded_process::flag_show_progress;
	try {
		if (items.get_count() == 0) throw pfc::exception_invalid_params();
		if (items.get_count() == 1) showProgress = 0;

		std::vector<hash_record> hash_record_list;
		for (size_t t = 0; t < items.get_count(); t++) {
			hash_record_list.push_back(hash_record(items[t]));
			clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(items[t], hash_record_list[t].hash);
			hash_record_list[t].record = getRecord(hash_record_list[t].hash);
		}

		service_ptr_t<threaded_process_callback> cb = new service_impl_t<get_lastfm_scrobbles>(hash_record_list);
		static_api_ptr_t<threaded_process>()->run_modeless(
			cb,
			showProgress | threaded_process::flag_show_item | threaded_process::flag_show_delayed,
			core_api::get_main_window(),
			COMPONENT_NAME": Retrieving last.fm scrobbles");
	} catch (std::exception const & e) {
		popup_message::g_complain("Could not retrieve last.fm scrobbles", e);
	}
}

