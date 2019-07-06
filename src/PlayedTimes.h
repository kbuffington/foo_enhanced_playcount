#pragma once
#include <vector>
#include "globals.h"

namespace foo_enhanced_playcount {
	void convertHashes(void);
#define kCurrVersion   1

	struct record_t {
		unsigned int version = kCurrVersion;
		unsigned int numFoobarPlays = 0;
		unsigned int numLastfmPlays = 0;
		int unused = 0;			// available for later
		std::vector<t_filetimestamp> foobarPlaytimes;
		std::vector<t_filetimestamp> lastfmPlaytimes;
	};

	typedef struct scrobbleData {
		pfc::string8 title;
		pfc::string8 artist;
		pfc::string8 album;
		t_filetimestamp scrobble_time;
		scrobbleData(pfc::string8 _title, pfc::string8 _artist, pfc::string8 _album, t_filetimestamp _time) : 
			title(_title), artist(_artist), album(_album), scrobble_time(_time) {};
	} scrobbleData;

	record_t getRecord(metadb_index_hash hash, const GUID index_guid = guid_foo_enhanced_playcount_index);
	void setRecord(metadb_index_hash hash, record_t record, const GUID index_guid = guid_foo_enhanced_playcount_index);
	void getFirstLastPlayedTimes(metadb_handle_ptr metadb_handle, record_t *record);
	
	struct hash_record {
		metadb_index_hash hash;
		metadb_handle_ptr mdb_handle;
		record_t record;
		hash_record(metadb_handle_ptr mdb_ptr) : mdb_handle(mdb_ptr) {}
	};

	void pull_scrobbles(metadb_handle_ptr metadb, bool refresh = true, bool recent = false);
	void GetLastfmScrobblesThreaded(metadb_handle_list_cref items, bool always_show_popup);
	void ClearLastFmRecords(metadb_handle_list_cref items);

	void updateRecentScrobblesThreaded(bool newScrobbles = true);
	void refreshThreadHashes(unsigned int updateCount);

	// A class that turns metadata + location info into hashes to which our data gets pinned by the backend.
	class metadb_index_client_impl : public metadb_index_client {
	public:
		metadb_index_client_impl(const char* pinTo, bool toLower);
		metadb_index_hash transform(const file_info& info, const playable_location& location);
	private:
		bool forceLowercase;
		titleformat_object::ptr m_keyObj;
	};

	metadb_index_client_impl* clientByGUID(const GUID& guid);
}