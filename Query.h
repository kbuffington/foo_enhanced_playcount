#pragma once

#if 0
namespace foo_enhanced_playback_statistics {
	class Query {
	public:
		Query(const char *entity, const char *id = "");
		void add_param(const char *param, const char *value, bool encode = true);
		void add_param(const char *param, int value);
		t_filetimestamp perform(abort_callback &callback = abort_callback_dummy());

	private:
		inline char to_hex(char);
		pfc::string8 url_encode(const char *);
		//TiXmlElement *parse(pfc::string8 &buffer, TiXmlDocument &xml);

		pfc::string8 url;
	};
}
#endif