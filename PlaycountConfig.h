#pragma once

namespace foo_enhanced_playcount {

	__declspec(selectany) extern char const* const DefaultLastfmUsername = "<none>";
	__declspec(selectany) extern char const* const DefaultLruCacheSize = "10";

	class PlaycountConfig : public cfg_var {
	public:
		PlaycountConfig();
		virtual ~PlaycountConfig() = default;

		bool EnableLastfmPlaycounts;
		bool IncrementLastfmWithPlaycount;
		bool UnusedBool1;
		bool UnusedBool2;
		bool UnusedBool3;

		pfc::string8 LastfmUsername;
		pfc::string8 LruCacheSize;
		pfc::string8 UnusedStr1;
		pfc::string8 UnusedStr2;
		pfc::string8 UnusedStr3;

	private:
		virtual void get_data_raw(stream_writer* p_stream, abort_callback& p_abort) override;
		virtual void set_data_raw(stream_reader* p_stream, t_size p_sizehint,
			abort_callback& p_abort) override;

		static unsigned const Version = 1;
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

} // namespace foo_scrobble