#include "stdafx.h"
#include "globals.h"
#include "PlaycountConfig.h"
#include "artistTimes.h"
#include <atomic>
#include <mutex>


using namespace foo_enhanced_playcount;
using namespace pfc;

namespace foo_enhanced_playcount {
	// Static cached ptr to metadb_index_manager
	// Cached because we'll be calling it a lot on per-track basis, let's not pass it everywhere to low level functions
	// Obtaining the pointer from core is reasonably efficient - log(n) to the number of known service classes, but not good enough for something potentially called hundreds of times
	static metadb_index_manager::ptr g_cachedAPI;
	static metadb_index_manager::ptr theAPI() {
		auto ret = g_cachedAPI;
		if (ret.is_empty()) ret = metadb_index_manager::get(); // since fb2k SDK v1.4, core API interfaces have a static get() method
		return ret;
	}

	artist_record_t getArtistRecord(metadb_index_hash hash, const GUID index_guid) {
		mem_block_container_impl temp; // this will receive our BLOB
		theAPI()->get_user_data(index_guid, hash, temp);
		if (temp.get_size() > 0) {
			try {
				// Parse the BLOB using stream formatters
				stream_reader_formatter_simple_ref< /* using big endian data? nope */ false > reader(temp.get_ptr(), temp.get_size());

				artist_record_t ret;
				reader >> ret.version;
				reader >> ret.artistLastPlayed;
				reader >> ret.artistUnused;

				if (reader.get_remaining() > 0) {
					// more data left in the stream?
					FB2K_console_formatter() << COMPONENT_NAME": Artist record has more data than expected.";
				}
				// if we attempted to read past the EOF, we'd land in the exception_io_data handler below

				return ret;
			} catch (exception_io_data) {
				// we get here as a result of stream formatter data error
				// fall thru to return a blank record
			}
		}
		return artist_record_t();
	}

	void setArtistRecord(metadb_index_hash hash, artist_record_t artist_rec, const GUID index_guid) {
		stream_writer_formatter_simple< /* using big endian data? nope */ false > writer;
		writer << artist_rec.version;
		writer << artist_rec.artistLastPlayed;
		writer << artist_rec.artistUnused;

		theAPI()->set_user_data(index_guid, hash, writer.m_buffer.get_ptr(), writer.m_buffer.get_size());
	}

	void setArtistLastPlayed(metadb_index_hash hash) {
		artist_record_t rec;
		
		rec.version = kCurrArtistRecordVersion;
		rec.artistLastPlayed = fileTimeNow();
		rec.artistUnused = kNoDate;
		setArtistRecord(hash, rec);
	}
}