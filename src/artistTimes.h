#pragma once
#include "globals.h"

namespace foo_enhanced_playcount {

#define kCurrArtistRecordVersion   1
	struct artist_record_t {
		unsigned int version = kCurrArtistRecordVersion;
		t_filetimestamp artistLastPlayed = kNoDate;
		t_filetimestamp artistUnused = kNoDate;
	};

	artist_record_t getArtistRecord(metadb_index_hash hash, const GUID index_guid = guid_foo_enhanced_playcount_artist_index);
	void setArtistRecord(metadb_index_hash hash, artist_record_t artist_rec, const GUID index_guid = guid_foo_enhanced_playcount_artist_index);
	void setArtistLastPlayed(metadb_index_hash hash);
}