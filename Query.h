#pragma once

namespace foo_enhanced_playcount {
	class Query {
	public:
		Query(const char *method = "user.getartisttracks");
		void add_param(const char *param, pfc::string8 value, bool encode = true);
		void add_param(const char *param, int value);
		void add_apikey();
		pfc::string8 perform(abort_callback &callback = abort_callback_dummy());

	private:
		inline char to_hex(char);
		pfc::string8 url_encode(pfc::string8 in, bool encodeSpecialChars = false);
		
		pfc::string8 url;
	};
}

