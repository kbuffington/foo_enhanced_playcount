#pragma once

namespace foo_enhanced_playcount {

	__declspec(selectany) extern char const* const DefaultLastfmUsername = "<none>";
	__declspec(selectany) extern char const* const DefaultLruCacheSize = "40";
	__declspec(selectany) extern char const* const DefaultArtistTfString = "%artist%";
	__declspec(selectany) extern char const* const DefaultAlbumTfString = "%album%";
	__declspec(selectany) extern char const* const DefaultTitleTfString = "%title%";

	class PlaycountConfig : public cfg_var {
	public:
		PlaycountConfig();
		virtual ~PlaycountConfig() = default;

		bool EnableLastfmPlaycounts;
		bool IncrementLastfmWithPlaycount;
		bool RemoveDuplicateLastfmScrobbles;
		bool autoPullScrobbles;
		bool UnusedBool1;
		bool UnusedBool2;
		bool UnusedBool3;
		bool UnusedBool4;
		bool CompareAlbumFields;
		bool delayScrobbleRetrieval;

		pfc::string8 LastfmUsername;
		pfc::string8 LruCacheSize;
		pfc::string8 ArtistTfString;
		pfc::string8 AlbumTfString;
		pfc::string8 TitleTfString;

		unsigned int CacheSize;
		t_filetimestamp latestScrobbleChecked;
		t_filetimestamp earliestScrobbleChecked;

	private:
		virtual void get_data_raw(stream_writer* p_stream, abort_callback& p_abort) override;
		virtual void set_data_raw(stream_reader* p_stream, t_size p_sizehint,
			abort_callback& p_abort) override;

		static unsigned const Version = 3;
		t_filetimestamp latestCache, earliestCache;	// we don't want to overwrite these typically
	};

	class NOVTABLE PlaycountConfigNotify : public service_base {
	public:
		virtual void OnConfigChanged() = 0;

		static void NotifyChanged() {
			if (core_api::assert_main_thread()) {
				service_enum_t<PlaycountConfigNotify> e;
				service_ptr_t<PlaycountConfigNotify> ptr;
				while (e.next(ptr))
					ptr->OnConfigChanged();
			}
		}

		FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(PlaycountConfigNotify)
	};

	extern PlaycountConfig Config;

} // namespace foo_enhanced_playcount