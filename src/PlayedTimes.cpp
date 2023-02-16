#include "stdafx.h"
#include "lastfm.h"
#include <vector>
#include <set>
#include <sstream>
#include <algorithm>
#include "util.h"
#include "globals.h"
#include "PlaycountConfig.h"
#include "PlayedTimes.h"
#include "artistTimes.h"
#include "thread_pool.h"
#include <atomic>
#include <mutex>


using namespace foo_enhanced_playcount;
using namespace pfc;

namespace foo_enhanced_playcount {

	PlaycountConfig const& config{ Config };

	// Pattern by which we pin our data to.
	// If multiple songs in the library evaluate to the same string,
	// they will be considered the same by our component,
	// and data applied to one will also show up with the rest.
	static const char strObsoletePinTo[] = "%artist% %album% $if2(%discnumber%,1) %tracknumber% %title%";
	static const char strPinTo[] = "%artist% - $year($if2(%date%,%original release date%)) - %album% $if2(%discnumber%,1)-%tracknumber% %title%";
	static const char strArtistPinTo[] = "%artist%";

	// Retain pinned data for four weeks if there are no matching items in library
	static const t_filetimestamp retentionPeriod = system_time_periods::week * 4;

	// mutexes and atomic bools
	std::atomic_uint32_t thread_counter = 0;
	std::atomic<bool> pullingRecentScrobbles = false, quitting = false;
	std::mutex retrieving_scrobbles;
	std::mutex adding_hashes_mutex;
	pfc::list_t<metadb_index_hash> thread_hashes;

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

	metadb_index_client_impl::metadb_index_client_impl(const char* pinTo, bool toLower = false) {
		static_api_ptr_t<titleformat_compiler>()->compile_force(m_keyObj, pinTo);
		forceLowercase = toLower;
	}

	metadb_index_hash metadb_index_client_impl::transform(const file_info& info, const playable_location& location) {
		pfc::string_formatter str, str_lower;
		pfc::string_formatter* strPtr = &str;
		m_keyObj->run_simple(location, &info, str);
		if (forceLowercase) {
			mb_to_lower(str, str_lower);
			strPtr = &str_lower;
		}
		// Make MD5 hash of the string, then reduce it to 64-bit metadb_index_hash
		return static_api_ptr_t<hasher_md5>()->process_single_string(*strPtr).xorHalve();
	}

	metadb_index_client_impl * clientByGUID(const GUID & guid) {
		// Static instances, never destroyed (deallocated with the process), created first time we get here
		// Using service_impl_single_t, reference counting disabled
		// This is somewhat ugly, operating on raw pointers instead of service_ptr, but OK for this purpose
		static metadb_index_client_impl * g_clientIndex = new service_impl_single_t<metadb_index_client_impl>(strPinTo, true);
		static metadb_index_client_impl* g_ArtistIndex = new service_impl_single_t<metadb_index_client_impl>(strArtistPinTo, true);
		static metadb_index_client_impl * g_clientObsolete = new service_impl_single_t<metadb_index_client_impl>(strObsoletePinTo);

		PFC_ASSERT(guid == guid_foo_enhanced_playcount_index ||
			guid == guid_foo_enhanced_playcount_obsolete ||
			guid == guid_foo_enhanced_playcount_artist_index);

		if (guid == guid_foo_enhanced_playcount_index) {
			return g_clientIndex;
		}
		else if (guid == guid_foo_enhanced_playcount_artist_index) {
			return g_ArtistIndex;
		}
		return g_clientObsolete;
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
					if (api->have_orphaned_data(guid_foo_enhanced_playcount_obsolete)) {
						dbNeedsConversion = true;
						FB2K_console_formatter() << COMPONENT_NAME": Found old index-db. Will convert hashes.";
						api->add(clientByGUID(guid_foo_enhanced_playcount_obsolete), guid_foo_enhanced_playcount_obsolete, retentionPeriod);
					}
					api->add(clientByGUID(guid_foo_enhanced_playcount_index), guid_foo_enhanced_playcount_index, retentionPeriod);
					api->add(clientByGUID(guid_foo_enhanced_playcount_artist_index), guid_foo_enhanced_playcount_artist_index, retentionPeriod);
				} catch (std::exception const & e) {
					api->remove(guid_foo_enhanced_playcount_index);
					api->remove(guid_foo_enhanced_playcount_artist_index);
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
			quitting = true;
			simple_thread_pool::instance().exit();
			// Cleanly kill g_cachedAPI before reaching static object destructors or else
			g_cachedAPI.release();
		}
	};
	static service_factory_single_t<init_stage_callback_impl> g_init_stage_callback_impl;
	static service_factory_single_t<initquit_impl> g_initquit_impl;

	void copyTimestampsToVector(t_filetimestamp *buf, const size_t numElements, std::vector<t_filetimestamp>& v) {
		v.insert(v.begin(), buf, buf + numElements);
	}


	record_t getRecord(metadb_index_hash hash, const GUID index_guid) {
		unsigned int buf[10004];
		record_t record;
		size_t size = 0;
		size = theAPI()->get_user_data_here(index_guid, hash, &buf, sizeof(buf));
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
				numElements = (int) size / sizeof(t_filetimestamp);
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

	std::mutex set_record_mutex;
	static void setRecord(metadb_index_hash hash, record_t record, const GUID index_guid) {
		unsigned int buf[10004];
		size_t size = 0;
		record.version = kCurrRecordVersion;
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

		std::lock_guard<std::mutex> guard(set_record_mutex);
		theAPI()->set_user_data(index_guid, hash, buf, size * sizeof(int));
	}

	class get_recent_scrobbles : public simple_thread_task {
	public:
		get_recent_scrobbles(std::vector<metadb_handle_ptr> items) : m_handles(items) {}

		void run() override
		{
			try {
				std::lock_guard<std::mutex> guard(retrieving_scrobbles);
				thread_counter = 0;

				for (auto const& handle : m_handles) {
					if (quitting) {
						break;
					}
					Sleep(300);	// background pulling so go slower than regular pulls & rate limit
					pull_scrobbles(handle, false, true);

					refreshThreadHashes(25);
				}
				while (thread_counter < m_handles.size() && !quitting) {
					// Is there a better way to wait for all the threads to complete?
					Sleep(10);
				}
				pfc::list_t<metadb_index_hash> refresh_hash_list(thread_hashes);
				thread_hashes.remove_all();
				fb2k::inMainThread([=] {
					theAPI()->dispatch_refresh(guid_foo_enhanced_playcount_index, refresh_hash_list);
				});
			}
			catch (std::exception const& e) {
				m_failMsg = e.what();
			}
			pullingRecentScrobbles = false;
		}
	private:
		pfc::string8 m_failMsg;
		const std::vector<metadb_handle_ptr> m_handles;
	};

	void updateSavedScrobbleTimes(t_filetimestamp scrobble_time, bool updateEarliest) {
		t_filetimestamp timestamp = fileTimeWtoU(scrobble_time) - 60;
		if (timestamp > Config.latestScrobbleChecked) {
			Config.latestScrobbleChecked = timestamp;
		}
		// it seems possible to update the earliest date to a date much earlier than the range being scrobbled,
		// so restrict updating this value to just tracks we don't need to pull from last.fm
		if (updateEarliest && timestamp < Config.earliestScrobbleChecked || !Config.earliestScrobbleChecked) {
			Config.earliestScrobbleChecked = timestamp;
		}
	}

	class updateRecentScrobbles : public simple_thread_task {
	public:
		updateRecentScrobbles(const bool pullNew, metadb_handle_list_cref library) :
			newScrobbles(pullNew), allLibraryItems(library) {}

		void run() override {
			if (Config.EnableLastfmPlaycounts) {
				pullingRecentScrobbles = true;
				Lastfm* lfm = new Lastfm();
				std::vector<scrobbleData> scrobble_vec =
					lfm->queryRecentTracks(newScrobbles, newScrobbles ? Config.latestScrobbleChecked : Config.earliestScrobbleChecked);
				std::vector<metadb_handle_ptr> handle_vec;
				std::set<metadb_index_hash> seenHashes; // list of all hash values so we can skip duplicates

#ifdef DEBUG
				t_filetimestamp start = filetimestamp_from_system_timer();
#endif
				pfc::array_t<bool> mask;
				mask.set_size(allLibraryItems.get_count());
				for (auto const& s : scrobble_vec)
				{
					search_filter_v2::ptr filter;
					string8 query;
					metadb_handle_list library = allLibraryItems;
					if (quitting) {
						break;
					}

					try {
						if (!s.artistHasQuotes) {
							query << "ARTIST IS \"" << s.artist << "\"";	// handles multiple values
						} else {
							query << "\"$stricmp(%artist%," << s.artist << ")\" IS 1";	// handles escaped double-quotes
						}
						query << " AND \"$stricmp(%title%," << s.title << ")\" IS 1";
						if (Config.CompareAlbumFields && s.album.length() > 0) {
							query << " AND \"$stricmp(%album%," << s.album << ")\" IS 1";
						}
						filter = search_filter_manager_v2::get()->create_ex(query, new service_impl_t<completion_notify_dummy>(), search_filter_manager_v2::KFlagSuppressNotify);
					}
					catch (const std::exception & e) {
						FB2K_console_formatter() << COMPONENT_NAME": Exception processing recent scrobbles: " << e.what();
					}
					filter->test_multi(library, mask.get_ptr());
					library.filter_mask(mask.get_ptr());
					if (library.get_count() > 0) {
						for (size_t i = 0; i < library.get_count(); i++) {
							metadb_index_hash hash;
							clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(library[i], hash);
							// have we seen this hash yet?
							if (seenHashes.find(hash) == std::end(seenHashes)) {
								seenHashes.insert(hash);

								record_t record = getRecord(hash);
								if ((!record.lastfmPlaytimes.size() || // if we don't have any scrobbles OR last known scrobble was more than 1 minute before scrobble_time
									(fileTimeWtoU(record.lastfmPlaytimes.back()) - 60) < s.scrobble_time) &&
									(s.scrobble_time - fileTimeWtoU(record.lastfmPlaytimes.back()) > 60)) {	// filtering for non-adjusted recorded scrobbles
									handle_vec.push_back(library[i]);
								}
								else if (record.lastfmPlaytimes.size()) {
									// we know about all scrobbles for this song so update earliest known scrobble time
									updateSavedScrobbleTimes(fileTimeUtoW(s.scrobble_time), true);
								}
							}
						}
					}
				}
#ifdef DEBUG
				t_filetimestamp end = filetimestamp_from_system_timer();
				FB2K_console_formatter() << "Calculating scrobbles to pull: " << (end - start) / 10000 << "ms";
#endif
				get_recent_scrobbles* task = new get_recent_scrobbles(handle_vec);
				if (!simple_thread_pool::instance().enqueue(task)) delete task;
			}
		}
	private:
		bool newScrobbles;
		metadb_handle_list allLibraryItems;
	};

	void updateRecentScrobblesThreaded(bool newScrobbles) {
		if (config.autoPullScrobbles) {
			metadb_handle_list allLibraryItems;
			library_manager::get()->get_all_items(allLibraryItems);
			if (!newScrobbles) {
				FB2K_console_formatter() << "Starting to pull legacy scrobbles (before " << Config.earliestScrobbleChecked << ")";
			}

			updateRecentScrobbles* task = new updateRecentScrobbles(newScrobbles, allLibraryItems);
			if (!simple_thread_pool::instance().enqueue(task)) delete task;
		}
	}

	titleformat_object::ptr artist_script;
	titleformat_object::ptr album_script;
	titleformat_object::ptr title_script;

	std::vector<t_filetimestamp> getLastFmPlaytimes(metadb_handle_ptr p_item, metadb_index_hash hash, const t_filetimestamp lastPlay) {
		std::vector<t_filetimestamp> playTimes;
		file_info_impl info;
		pfc::string_formatter artist, title, album = "";

		if (config.EnableLastfmPlaycounts && p_item->get_info(info)) {
			if (artist_script.is_empty()) {
				static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(artist_script, config.ArtistTfString);
			}
			if (album_script.is_empty()) {
				static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(album_script, config.AlbumTfString);
			}
			if (title_script.is_empty()) {
				static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(title_script, config.TitleTfString);
			}

			p_item->format_title(NULL, artist, artist_script, NULL);
			if (config.CompareAlbumFields) {
				p_item->format_title(NULL, album, album_script, NULL);
			}
			p_item->format_title(NULL, title, title_script, NULL);

			if (artist.get_length() > 0 && title.get_length() > 0 &&
					info.get_length() > 29) {	// you can't scrobble a song less than 30 seconds long, so don't check to see if it was scrobbled.
				pfc::string8 time;
#ifdef DEBUG
				t_filetimestamp start = filetimestamp_from_system_timer();
#endif

				Lastfm *lfm = new Lastfm(hash, artist, album, title);
				playTimes = lfm->queryByTrack(lastPlay);
#ifdef DEBUG
				t_filetimestamp end = filetimestamp_from_system_timer();
				time << "Time Elapsed: " << (end - start) / 10000 << "ms - ";
#endif
				pfc::string8 lastPlayMsg;
				if (lastPlay > 0) {
					lastPlayMsg << " (since last known scrobble at " << foobar2000_io::format_filetimestamp(lastPlay) << ")";
				}
				FB2K_console_formatter() << time << "Found " << playTimes.size() << " scrobbles in last.fm" << lastPlayMsg << " of \"" << title << "\"";
			}
		}

		return playTimes;
	}


	titleformat_object::ptr first_and_last_played_script;

	class my_playback_statistics_collector : public playback_statistics_collector {
	public:
		void on_item_played(metadb_handle_ptr p_item) {
			metadb_index_hash hash, artistHash;
			clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(p_item, hash);
			clientByGUID(guid_foo_enhanced_playcount_artist_index)->hashHandle(p_item, artistHash);

			record_t record = getRecord(hash);
			if (record.numFoobarPlays == 0) {
				getFirstLastPlayedTimes(p_item, &record);
			}
			t_filetimestamp time = filetimestamp_from_system_timer();
			time /= 10000000;
			time *= 10000000;
			record.foobarPlaytimes.push_back(time);
			record.numFoobarPlays = (unsigned int) record.foobarPlaytimes.size();

			setRecord(hash, record);
			setArtistLastPlayed(artistHash);
		}
	};

	static playback_statistics_collector_factory_t<my_playback_statistics_collector> g_my_stat_collector;

	class metadb_io_edit_callback_impl : public metadb_io_edit_callback {
	public:
		typedef const pfc::list_base_const_t<const file_info*> & t_infosref;
		void on_edited(metadb_handle_list_cref items, t_infosref before, t_infosref after) {
			for (size_t t = 0; t < items.get_count(); t++) {
				metadb_index_hash hashOld, hashNew;
				static hasher_md5::ptr hasher = hasher_md5::get();

				clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(items[t], hashOld);

				auto playable_location = make_playable_location(items[t]->get_path(), items[t]->get_subsong_index());
				hashNew = clientByGUID(guid_foo_enhanced_playcount_index)->transform(*after[t], playable_location);
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

	// Context Menu functions start here
	void ClearLastFmRecords(metadb_handle_list_cref items) {
		try {
			if (items.get_count() == 0) throw pfc::exception_invalid_params();

			pfc::avltree_t<metadb_index_hash> tmp;

			for (size_t t = 0; t < items.get_count(); t++) {
				metadb_index_hash hash;
				clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(items[t], hash);

				record_t record = getRecord(hash);
				record.lastfmPlaytimes.clear();
				record.numLastfmPlays = 0;
				setRecord(hash, record);
				tmp += hash;
			}

			pfc::list_t<metadb_index_hash> hashes;
			for (auto iter = tmp.first(); iter.is_valid(); ++iter)
			{
				const metadb_index_hash hash = *iter;
				hashes += hash;
			}

			theAPI()->dispatch_refresh(guid_foo_enhanced_playcount_index, hashes);

		} catch (std::exception const & e) {
			popup_message::g_complain("Could not remove last.fm plays", e);
		}
	}

	class get_lastfm_scrobble : public simple_thread_task {
	public:
		get_lastfm_scrobble(const metadb_handle_ptr& handle, const bool refresh, const bool recent) :
			m_handle(handle), doRefresh(refresh), isRecent(recent) {}

		void run() override
		{
			metadb_index_hash hash;
			clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(m_handle, hash);

			record_t record = getRecord(hash);
			std::vector<t_filetimestamp> playTimes;
			playTimes = getLastFmPlaytimes(m_handle, hash,
				record.lastfmPlaytimes.size() ? record.lastfmPlaytimes.back() : 0);
			if (playTimes.size()) {
				record.lastfmPlaytimes.insert(record.lastfmPlaytimes.end(), playTimes.begin(), playTimes.end());
				record.numLastfmPlays = (unsigned int) record.lastfmPlaytimes.size();

				if (record.numFoobarPlays == 0) {
					getFirstLastPlayedTimes(m_handle, &record);
				}

				setRecord(hash, record);
			}

			if (doRefresh && playTimes.size()) {
				// non-threaded path
				pfc::list_t<metadb_index_hash> hashes;
				hashes += hash;
				fb2k::inMainThread([=] {
					theAPI()->dispatch_refresh(guid_foo_enhanced_playcount_index, hashes);
				});
			} else {
				thread_counter++;
				std::lock_guard<std::mutex> guard(adding_hashes_mutex);
				thread_hashes += hash;
				if (isRecent && record.lastfmPlaytimes.size()) {
					updateSavedScrobbleTimes(record.lastfmPlaytimes.back(), false);
				}
			}
		}

	private:
		metadb_handle_ptr m_handle;
		bool doRefresh, isRecent;
	};

	void refreshThreadHashes(unsigned int updateCount) {
		if (thread_hashes.get_count() >= updateCount) {
			std::lock_guard<std::mutex> guard(adding_hashes_mutex);
			pfc::list_t<metadb_index_hash> refresh_hash_list(thread_hashes);
			thread_hashes.remove_all();
			fb2k::inMainThread([=] {
				theAPI()->dispatch_refresh(guid_foo_enhanced_playcount_index, refresh_hash_list);
			});
		}
	}

	void pull_scrobbles(metadb_handle_ptr metadb, bool refresh, bool recent) {
		get_lastfm_scrobble* task = new get_lastfm_scrobble(metadb, refresh, recent);
		if (!simple_thread_pool::instance().enqueue(task)) delete task;
	}

	class get_lastfm_scrobbles : public threaded_process_callback {
	public:
		get_lastfm_scrobbles(std::vector<hash_record> items) : m_items(items) {}
		void on_init(HWND p_wnd) {}
		void run(threaded_process_status & p_status, abort_callback & p_abort) {
			try {
				std::lock_guard<std::mutex> guard(retrieving_scrobbles);	// TODO: disable option while pulling scrobbles
				thread_counter = 0;

				for (size_t t = 0; t < m_items.size(); t++) {
					if (quitting) {
						break;
					}
					p_status.set_progress(t, m_items.size());
					p_status.set_item_path(m_items[t].mdb_handle->get_path());

					if (t >= 5) {
						Sleep(200);	// rate limited to 5 requests per second, so wait 200ms between requests, but don't sleep on first 5 requests
					}
					pull_scrobbles(m_items[t].mdb_handle, false);

					refreshThreadHashes(25);
				}
				while (thread_counter < m_items.size() && !quitting) {
					// Is there a better way to wait for all the threads to complete?
					Sleep(10);
				}
				pfc::list_t<metadb_index_hash> refresh_hash_list(thread_hashes);
				thread_hashes.remove_all();
				fb2k::inMainThread([=] {
					theAPI()->dispatch_refresh(guid_foo_enhanced_playcount_index, refresh_hash_list);
				});

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

	void getFirstLastPlayedTimes(metadb_handle_ptr metadb_handle, record_t *record) {
		t_filetimestamp fp = 0, lp = 0;
		if (first_and_last_played_script.is_empty()) {
			static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(first_and_last_played_script, "%first_played%~%last_played%");
		}
		pfc::string_formatter p_out;

		metadb_handle->format_title(NULL, p_out, first_and_last_played_script, NULL);
		t_size divider = p_out.find_first('~');
		char firstPlayed[25], lastPlayed[25];
		strncpy_s(firstPlayed, p_out.toString(), divider);
		strcpy_s(lastPlayed, p_out.toString() + divider + 1);

		if (strcmp(firstPlayed, "N/A") && strcmp(firstPlayed, "?")) {
			fp = foobar2000_io::filetimestamp_from_string(firstPlayed);
			lp = foobar2000_io::filetimestamp_from_string(lastPlayed);

			record->foobarPlaytimes.push_back(fp);
			if (fp != lp) {
				record->foobarPlaytimes.push_back(lp);
			}
			record->numFoobarPlays = (unsigned int) record->foobarPlaytimes.size();
		}
	}

	void GetLastfmScrobblesThreaded(metadb_handle_list_cref items, bool always_show_popup) {
		int threaded_process_flags = threaded_process::flag_show_progress | threaded_process::flag_show_delayed | threaded_process::flag_show_item;
		try {
			if (items.get_count() == 0) throw pfc::exception_invalid_params();
			if (items.get_count() == 1) threaded_process_flags &= ~threaded_process::flag_show_progress;
			if (always_show_popup) threaded_process_flags &= ~threaded_process::flag_show_delayed;

			std::vector<hash_record> hash_record_list;
			for (size_t t = 0; t < items.get_count(); t++) {
				hash_record_list.push_back(hash_record(items[t]));
				clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(items[t], hash_record_list[t].hash);
				hash_record_list[t].record = getRecord(hash_record_list[t].hash);
			}

			// TODO: stop using hash_record_list and replace with vector<metadb_handle_ptr>
			service_ptr_t<threaded_process_callback> cb = new service_impl_t<get_lastfm_scrobbles>(hash_record_list);
			static_api_ptr_t<threaded_process>()->run_modeless(
				cb,
				threaded_process_flags,
				core_api::get_main_window(),
				COMPONENT_NAME": Retrieving last.fm scrobbles");
		} catch (std::exception const & e) {
			popup_message::g_complain("Could not retrieve last.fm scrobbles", e);
		}
	}

	class my_play_callback : public play_callback_static {
	public:
		void on_playback_new_track(metadb_handle_ptr metadb) {
			m_elapsed = 0;
			if (config.EnableLastfmPlaycounts && !config.delayScrobbleRetrieval) {
				pull_scrobbles(metadb);
			}
			tracksSinceScrobblePull++;
			tracksSinceNewScrobblePull++;
			if (config.EnableLastfmPlaycounts && config.autoPullScrobbles && !pullingRecentScrobbles) {
				if (tracksSinceScrobblePull >= 12) {
					updateRecentScrobblesThreaded(false);
					tracksSinceScrobblePull = 0;
				}
				if (tracksSinceNewScrobblePull >= 5) {
					updateRecentScrobblesThreaded(true);
					tracksSinceNewScrobblePull = 0;
				}
			}
		}
		void on_playback_starting(play_control::t_track_command p_command, bool p_paused) {}
		void on_playback_stop(play_control::t_stop_reason p_reason) {}
		void on_playback_seek(double p_time) {}
		void on_playback_pause(bool p_state) {}
		void on_playback_edited(metadb_handle_ptr p_track) {}
		void on_playback_dynamic_info(const file_info & p_info) {}
		void on_playback_dynamic_info_track(const file_info & p_info) {}
		void on_playback_time(double p_time) {
			m_elapsed++;
			if (m_elapsed == 2 && config.EnableLastfmPlaycounts && config.delayScrobbleRetrieval) {
				metadb_handle_ptr metadb;
				if (playback_control::get()->get_now_playing(metadb)) {
					pull_scrobbles(metadb);
				}
			}
		}
		void on_volume_change(float p_new_val) {}

		/* The play_callback_manager enumerates play_callback_static services and registers them automatically. We only have to provide the flags indicating which callbacks we want. */
		virtual unsigned get_flags() {
			return flag_on_playback_new_track | flag_on_playback_time;
		}
	private:
		size_t m_elapsed = 0, tracksSinceScrobblePull = 0, tracksSinceNewScrobblePull = 0;
	};

	static service_factory_single_t<my_play_callback> g_play_callback_static_factory;
}
