#include "stdafx.h"
#include "lastfm.h"
#include <vector>
#include <sstream>


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

namespace {
	
	// Replace with your own when reusing.
	// Always recreate guid_foo_enhanced_playcount_index if your metadb_index_client_impl hashing semantics changed or else you run into inconsistent/nonsensical data.
	static const GUID guid_foo_enhanced_playcount_index = { 0xc1bd000, 0x43e7, 0x4078, { 0xb8, 0x85, 0x48, 0xee, 0x42, 0x49, 0xde, 0xc2 } };

	// Pattern by which we pin our data to. 
	// If multiple songs in the library evaluate to the same string, 
	// they will be considered the same by our component, 
	// and data applied to one will also show up with the rest.
	static const char strPinTo[] = "%artist% %album% $if2(%discnumber%,1) %tracknumber% %title%";
	
	// Our group in Properties dialog / Details tab, see track_property_provider_impl
	//static const char strPropertiesGroup[] = "Sample Component";

	// Retain pinned data for four weeks if there are no matching items in library
	static const t_filetimestamp retentionPeriod = system_time_periods::week * 4;

	// Returns a titleformat_object used to generate key values for our database.
	static titleformat_object::ptr makeKeyObj(const char * pinTo) {
		titleformat_object::ptr obj;
		static_api_ptr_t<titleformat_compiler>()->compile_force(obj, pinTo);
		return obj;
	}

	// A class that turns metadata + location info into hashes to which our data gets pinned by the backend.
	class metadb_index_client_impl : public metadb_index_client {
	public:
		metadb_index_hash transform(const file_info & info, const playable_location & location) {
			static auto obj = makeKeyObj(strPinTo); // initialized first time we get here and reused later
			pfc::string_formatter str;
			obj->run_simple( location, &info, str );
			// Make MD5 hash of the string, then reduce it to 64-bit metadb_index_hash
			return static_api_ptr_t<hasher_md5>()->process_single_string( str ).xorHalve();
		}
	};
	
	// Static instance, never destroyed (dies with the process)
	// Uses service_impl_single_t, reference counting disabled
	static metadb_index_client_impl * g_client = new service_impl_single_t<metadb_index_client_impl>;
	

	// An init_stage_callback to hook ourselves into the metadb
	// We need to do this properly early to prevent dispatch_global_refresh() from new fields that we added from hammering playlists etc
	class init_stage_callback_impl : public init_stage_callback {
	public:
		void on_init_stage(t_uint32 stage) {
			if (stage == init_stages::before_config_read) {
				static_api_ptr_t<metadb_index_manager> api;
				// Important, handle the exceptions here!
				// This will fail if the files holding our data are somehow corrupted.
				try {
					api->add(g_client, guid_foo_enhanced_playcount_index, retentionPeriod);
				} catch (std::exception const & e) {
					api->remove(guid_foo_enhanced_playcount_index);
					FB2K_console_formatter() << "[foo_enhanced_playcount] Critical initialization failure: " << e;
					return;
				}
				api->dispatch_global_refresh();
			}
		}
	};

	static service_factory_single_t<init_stage_callback_impl> g_init_stage_callback_impl;

	struct record_t {
		int version = 1;
		t_filetimestamp *fbTimes;
		t_filetimestamp *lastfmTimes;
	};

	static std::vector<t_filetimestamp> playtimes_get(metadb_index_hash hash, static_api_ptr_t<metadb_index_manager> & api) {
		std::vector<t_filetimestamp> playTimes;
		t_filetimestamp playTimeArray[1000];
		int size = api->get_user_data_here(guid_foo_enhanced_playcount_index, hash, &playTimeArray, sizeof(playTimeArray));
		int numElements = size / sizeof(t_filetimestamp);
		for (int i = 0; i < numElements; i++) {
			playTimes.push_back(playTimeArray[i]);
		}
		//FB2K_console_formatter() << "[foo_enhanced_playcount]: numElements = " << numElements;
		return playTimes;
	}

	static std::vector<t_filetimestamp> playtimes_get(metadb_index_hash hash) {
		static_api_ptr_t<metadb_index_manager> api;
		return playtimes_get(hash, api);
	}

	titleformat_object::ptr playback_statistics_script;

	static void playtime_set(metadb_index_hash hash, t_filetimestamp fp, t_filetimestamp lp) {
		t_filetimestamp time = filetimestamp_from_system_timer();
		t_filetimestamp playTimeList[1000];
		static_api_ptr_t<metadb_index_manager> api;
		size_t size = api->get_user_data_here(guid_foo_enhanced_playcount_index, hash, playTimeList, sizeof(playTimeList));
		size_t numElements = size / sizeof(t_filetimestamp);
		int index = numElements;
		if (numElements == 0 && fp) {	// add first played and last played if this is the first time we've recorded a play for this file
			playTimeList[index++] = fp;
			if (fp != lp) {
				playTimeList[index++] = lp;
			}
		}
		playTimeList[index++] = time;
		//FB2K_console_formatter() << "[foo_enhanced_playcount]: numElements = " << numElements << " - updating numElements to " << index << " " << fp;
		api->set_user_data(guid_foo_enhanced_playcount_index, hash, &playTimeList, sizeof(t_filetimestamp) * index);
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
			char firstPlayed[50], lastPlayed[25];
			strncpy_s(firstPlayed, p_out.toString(), divider);
			strcpy_s(lastPlayed, p_out.toString() + divider + 1);
			t_filetimestamp fp = 0, lp = 0;
			
			if (strcmp(firstPlayed, "N/A")) {
				fp = foobar2000_io::filetimestamp_from_string(firstPlayed);
				lp = foobar2000_io::filetimestamp_from_string(lastPlayed);
			}
			
			//console::printf(p_out);
			//FB2K_console_formatter() << firstPlayed << " - " << lastPlayed;
			//FB2K_console_formatter() << format_filetimestamp::format_filetimestamp(fp) << " - " << format_filetimestamp::format_filetimestamp(lp);

			playtime_set(hash, fp, lp);
		}
	};

	static playback_statistics_collector_factory_t<my_playback_statistics_collector> g_my_stat_collector;

	std::string t_uint64_to_string(t_uint64 value, bool jsTimestamp) {
		std::ostringstream os;
		if (jsTimestamp) {
			t_uint64 jsValue = (value - 116444736000000000) / 10000000;	// convert to JS timestamp
			jsValue *= 1000;	// strip out ms
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

#define kNumFields	3

	// Provider of the %foo_sample_rating% field
	class metadb_display_field_provider_impl : public metadb_display_field_provider {
	public:
		t_uint32 get_field_count() {
			return kNumFields;
		}
		void get_field_name(t_uint32 index, pfc::string_base & out) {
			PFC_ASSERT(index >= 0 && index < kNumFields);
			switch (index) {
				case 0:
					out = "played_times";
					break;
				case 1:
					out = "played_times_js";
					break;
				case 2:
					out = "played_times_raw";
					break;
			}			
		}
		bool process_field(t_uint32 index, metadb_handle * handle, titleformat_text_out * out) {
			PFC_ASSERT(index >= 0 && index < kNumFields);
			metadb_index_hash hash;
			if (!g_client->hashHandle(handle, hash)) return false;
			std::vector<t_filetimestamp> playTimes;
			file_info_impl info;

			switch (index) {
				case 0:
					playTimes = playtimes_get(hash);
					if (!playTimes.size()) {
						out->write(titleformat_inputtypes::meta, "[]");
					} else {
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, true, false).c_str());
					}
					break;
				case 1:
					playTimes = playtimes_get(hash);
					if (!playTimes.size()) {
						out->write(titleformat_inputtypes::meta, "[]");
					} else {
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, false, true).c_str());
					}


					if (handle->get_info(info)) {
						pfc::string8 artist;
						pfc::string8 album;
						pfc::string8 title;
						artist = info.meta_get("ARTIST", 0);
						album = info.meta_get("ALBUM", 0);
						title = info.meta_get("TITLE", 0);
						
						std::vector<t_filetimestamp> playTimes;
						Lastfm *lfm = new Lastfm();
						playTimes = lfm->queryLastfm(artist, album, title);
						
						std::string str;
						int idx = 0;
						for (std::vector<t_filetimestamp>::reverse_iterator rit = playTimes.rbegin(); rit != playTimes.rend(); ++rit, ++idx) {
							str.append(format_filetimestamp::format_filetimestamp(*rit));
							//str += t_uint64_to_string(*rit, true);
							if (idx + 1< playTimes.size()) {
								str.append(", ");
							}
						}
						FB2K_console_formatter() << str.c_str();
						
#if 0
						titleformat_object::ptr artist_album_title;

						if (artist_album_title.is_empty()) {
							static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(artist_album_title, "%artist% - %album% - %title%");
						}
						pfc::string_formatter p_out;

						//metadb_index_hash hash;
						//g_client->hashHandle(p_item, hash);

						handle->format_title(NULL, p_out, artist_album_title, NULL);
						FB2K_console_formatter() << p_out << "  -  " << artist << " - " << album << " - " << title;
#endif

						//Query *query = new Query();
						//query->add_apikey();
						//query->add_param("user", "MordredKLB");
						//query->add_param("artist", artist);
						//query->add_param("limit", 1);
						//query->add_param("format", "json");
						//query->perform();
					}

					break;
				case 2:
					playTimes = playtimes_get(hash);
					if (!playTimes.size()) {
						out->write(titleformat_inputtypes::meta, "[]");
					} else {
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, false, false).c_str());
					}
					break;
			}
			return true;
		}
	};

	static service_factory_single_t<metadb_display_field_provider_impl> g_metadb_display_field_provider_impl;
}
