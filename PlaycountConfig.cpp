#include "stdafx.h"
#include "PlaycountConfig.h"

namespace foo_enhanced_playcount
{

// {D64C1FC5-6184-406A-A041-3E7B3B56EE5A}
static const GUID PlaycountConfigId =
	{ 0xd64c1fc5, 0x6184, 0x406a, { 0xa0, 0x41, 0x3e, 0x7b, 0x3b, 0x56, 0xee, 0x5a } };


PlaycountConfig::PlaycountConfig()
    : cfg_var(PlaycountConfigId)
	, EnableLastfmPlaycounts(true)
	, LastfmUsername(DefaultLastfmUsername)
{
}

void PlaycountConfig::get_data_raw(stream_writer* p_stream, abort_callback& p_abort)
{
    p_stream->write_lendian_t(Version, p_abort);

	p_stream->write_lendian_t(EnableLastfmPlaycounts, p_abort);

	p_stream->write_string(LastfmUsername, p_abort);

}

void SetData(PlaycountConfig& cfg, stream_reader* p_stream, abort_callback& p_abort)
{
	p_stream->read_lendian_t(cfg.EnableLastfmPlaycounts, p_abort);

	p_stream->read_string(cfg.LastfmUsername, p_abort);

}

void PlaycountConfig::set_data_raw(stream_reader* p_stream, t_size /*p_sizehint*/,
                                  abort_callback& p_abort)
{
    unsigned version;
    p_stream->read_lendian_t(version, p_abort);

    switch (version) {
    case 1:
        SetData(*this, p_stream, p_abort);
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

// {FC257E75-C504-41FC-ADAA-CEF0C68188A5}
const GUID PlaycountConfigNotify::class_guid =
	{ 0xfc257e75, 0xc504, 0x41fc, { 0xad, 0xaa, 0xce, 0xf0, 0xc6, 0x81, 0x88, 0xa5 } };


PlaycountConfig Config;

} // namespace foo_scrobble
