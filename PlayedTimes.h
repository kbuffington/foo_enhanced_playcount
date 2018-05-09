#pragma once
#include <vector>

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
}