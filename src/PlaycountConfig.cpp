#include "stdafx.h"
#include "PlaycountConfig.h"

namespace foo_enhanced_playcount
{

// {9D604BE2-C8F3-4CB3-92BC-57CDFEEE6184}
static const GUID PlaycountConfigId =
	{ 0x9d604be2, 0xc8f3, 0x4cb3,{ 0x92, 0xbc, 0x57, 0xcd, 0xfe, 0xee, 0x61, 0x84 } };


PlaycountConfig::PlaycountConfig()
	: cfg_var(PlaycountConfigId)
	, EnableLastfmPlaycounts(false)
	, IncrementLastfmWithPlaycount(true)
	, RemoveDuplicateLastfmScrobbles(true)
	, autoPullScrobbles(true)
	, UnusedBool1(false)
	, UnusedBool2(false)
	, UnusedBool3(true)
	, UnusedBool4(true)
	, CompareAlbumFields(true)
	, delayScrobbleRetrieval(true)
	, LastfmUsername(DefaultLastfmUsername)
	, LruCacheSize(DefaultLruCacheSize)
	, ArtistTfString(DefaultArtistTfString)
	, AlbumTfString(DefaultAlbumTfString)
	, TitleTfString(DefaultTitleTfString)
	, CacheSize(0)
	, latestScrobbleChecked(0)
	, earliestScrobbleChecked(0)
{
}

// writes/saves data from cfg_var object into foobar config file
void PlaycountConfig::get_data_raw(stream_writer* p_stream, abort_callback& p_abort)
{
    p_stream->write_lendian_t(Version, p_abort);

	p_stream->write_lendian_t(EnableLastfmPlaycounts, p_abort);
	p_stream->write_lendian_t(IncrementLastfmWithPlaycount, p_abort);
	p_stream->write_lendian_t(RemoveDuplicateLastfmScrobbles, p_abort);
	p_stream->write_lendian_t(UnusedBool1, p_abort);
	p_stream->write_lendian_t(CompareAlbumFields, p_abort);
	p_stream->write_lendian_t(delayScrobbleRetrieval, p_abort);

	p_stream->write_string(LastfmUsername, p_abort);
	CacheSize = std::stoi(LruCacheSize.c_str());
	if (CacheSize > 50) {
		LruCacheSize = "50";
		CacheSize = 50;
	}
	p_stream->write_string(LruCacheSize, p_abort);
	p_stream->write_string(ArtistTfString, p_abort);
	p_stream->write_string(AlbumTfString, p_abort);
	p_stream->write_string(TitleTfString, p_abort);

	p_stream->write_lendian_t(latestScrobbleChecked, p_abort);
	p_stream->write_lendian_t(earliestScrobbleChecked, p_abort);
	p_stream->write_lendian_t(autoPullScrobbles, p_abort);
	p_stream->write_lendian_t(UnusedBool2, p_abort);
	p_stream->write_lendian_t(UnusedBool3, p_abort);
	p_stream->write_lendian_t(UnusedBool4, p_abort);
}

// Reads data from config file and sets in cfg_var object
void SetData(PlaycountConfig& cfg, stream_reader* p_stream, abort_callback& p_abort, unsigned version)
{
	p_stream->read_lendian_t(cfg.EnableLastfmPlaycounts, p_abort);
	p_stream->read_lendian_t(cfg.IncrementLastfmWithPlaycount, p_abort);
	p_stream->read_lendian_t(cfg.RemoveDuplicateLastfmScrobbles, p_abort);
	p_stream->read_lendian_t(cfg.UnusedBool1, p_abort);
	p_stream->read_lendian_t(cfg.CompareAlbumFields, p_abort);
	p_stream->read_lendian_t(cfg.delayScrobbleRetrieval, p_abort);

	p_stream->read_string(cfg.LastfmUsername, p_abort);
	p_stream->read_string(cfg.LruCacheSize, p_abort);
	p_stream->read_string(cfg.ArtistTfString, p_abort);
	p_stream->read_string(cfg.AlbumTfString, p_abort);
	p_stream->read_string(cfg.TitleTfString, p_abort);

	if (cfg.ArtistTfString.get_length() == 0) {
		cfg.ArtistTfString = DefaultArtistTfString;
	}
	if (cfg.AlbumTfString.get_length() == 0) {
		cfg.AlbumTfString = DefaultAlbumTfString;
	}
	if (cfg.TitleTfString.get_length() == 0) {
		cfg.TitleTfString = DefaultTitleTfString;
	}

	if (version == 1 && std::stoi(cfg.LruCacheSize.c_str()) < 40) {
		cfg.LruCacheSize = DefaultLruCacheSize; // increase default cache size
		cfg.CacheSize = std::stoi(DefaultLruCacheSize);
	} else {
		cfg.CacheSize = std::stoi(cfg.LruCacheSize.c_str());
	}
	if (version < 3) {
		cfg.earliestScrobbleChecked = 0;
		cfg.latestScrobbleChecked = 0;
		cfg.autoPullScrobbles = true;
		cfg.UnusedBool2 = false;
		cfg.UnusedBool3 = true;
		cfg.UnusedBool4 = true;
	} else {
		//t_filetimestamp t;
		//p_stream->read_lendian_t(t, p_abort);
		//cfg.latestScrobbleChecked = 1563892995;
		p_stream->read_lendian_t(cfg.latestScrobbleChecked, p_abort);
		//p_stream->read_lendian_t(t, p_abort);
		p_stream->read_lendian_t(cfg.earliestScrobbleChecked, p_abort);
		p_stream->read_lendian_t(cfg.autoPullScrobbles, p_abort);
		p_stream->read_lendian_t(cfg.UnusedBool2, p_abort);
		p_stream->read_lendian_t(cfg.UnusedBool3, p_abort);
		p_stream->read_lendian_t(cfg.UnusedBool4, p_abort);
	}
}

void PlaycountConfig::set_data_raw(stream_reader* p_stream, t_size p_sizehint,
                                  abort_callback& p_abort)
{
    unsigned version;
    p_stream->read_lendian_t(version, p_abort);

    switch (version) {
		case 1:
		case 2:
		case 3:
			SetData(*this, p_stream, p_abort, version);
			break;
    }

	/* // Not needing any title formatting stuff in config at the moment, but might later on
    std::pair<pfc::string8*, char const*> mappings[] = {
        {&ArtistMapping, DefaultArtistMapping},
        {&AlbumMapping, DefaultAlbumMapping},
        {&AlbumArtistMapping, DefaultAlbumArtistMapping},
        {&TitleMapping, DefaultTitleMapping},
        {&MBTrackIdMapping, DefaultMBTrackIdMapping},
        {&SkipSubmissionFormat, ""},
    };

    static_api_ptr_t<titleformat_compiler> compiler;
    for (auto&& mapping : mappings) {
        titleformat_object::ptr obj;
        if (!compiler->compile(obj, mapping.first->c_str()))
            *mapping.first = mapping.second;
    }
	*/
}

// {2E77400D-F82E-4A3A-B049-38077D78A6DE}
const GUID PlaycountConfigNotify::class_guid =
	{ 0x2e77400d, 0xf82e, 0x4a3a,{ 0xb0, 0x49, 0x38, 0x7, 0x7d, 0x78, 0xa6, 0xde } };


PlaycountConfig Config;

} // namespace foo_enhanced_playcount
