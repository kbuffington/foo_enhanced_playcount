#pragma once
#include <vector>
#include "globals.h"

namespace enhanced_playcount {
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

	record_t getRecord(metadb_index_hash hash, const GUID index_guid = guid_foo_enhanced_playcount_index);
	void setRecord(metadb_index_hash hash, record_t record, const GUID index_guid = guid_foo_enhanced_playcount_index);
	void getFirstLastPlayedTimes(metadb_handle_ptr metadb_handle, record_t *record);
	
	struct hash_record {
		metadb_index_hash hash;
		metadb_handle_ptr mdb_handle;
		record_t record;
		hash_record(metadb_handle_ptr mdb_ptr) : mdb_handle(mdb_ptr) {}
	};

	void GetLastfmScrobblesThreaded(metadb_handle_list_cref items, bool always_show_popup);
	void ClearLastFmRecords(metadb_handle_list_cref items);
}