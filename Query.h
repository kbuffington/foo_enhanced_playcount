#pragma once
#include "LruCache.cpp"

namespace foo_enhanced_playcount {
	class Query {
	public:
		Query(const char *method = "user.getartisttracks");
		void add_param(const char *param, pfc::string8 value, bool encode = true);
		void add_param(const char *param, int value);
		void add_param(const char *param, unsigned int value);
		void add_apikey();
		pfc::string8 perform(metadb_index_hash hash, abort_callback &callback = abort_callback_dummy());
		pfc::string8 getCacheSize();
		int Query::setCacheSize(int requestedSize);

	private:
		inline char to_hex(char);
		pfc::string8 url_encode(pfc::string8 in);

		pfc::string8 url;
	};
}

