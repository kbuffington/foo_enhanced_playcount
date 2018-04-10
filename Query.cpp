#include "stdafx.h"
#include "globals.h"
#include "Query.h"
#include "LruCache.cpp"
#include "PlaycountConfig.h"

static const pfc::string8 lastfmApiKey = "a1685abe5265b93cf2be4a70d181bf6b";

// http://ws.audioscrobbler.com/2.0/?method=user.getartisttracks&user=MordredKLB&artist=metallica&api_key=a1685abe5265b93cf2be4a70d181bf6b&format=json

static const char lastfmApiBase[] = "http://ws.audioscrobbler.com/2.0/?method=";


using namespace foo_enhanced_playcount;
using namespace pfc;

bool initializedCache = false;
extern PlaycountConfig const& config;

struct CacheObj {
	std::vector<metadb_index_hash> readHashes;
	std::string response;
};

LruCache<int, CacheObj> pageCache(0);

Query::Query(const char *method) {
	if (!initializedCache) {
		pageCache.setCacheSize(config.CacheSize);
		initializedCache = true;
	}
	url << lastfmApiBase << method;
}

pfc::string8 Query::getCacheSize() {
	std::string cacheVals;
	//pfc::string8 sizeStr = std::to_string(pageCache.getCacheSize(cacheVals)).c_str();
	pfc::string8 sizeStr = std::to_string(pageCache.getCacheSize()).c_str();
	//size_t memSize = cacheVals.size();

	//sizeStr << " [" << (memSize / 1024) << "kB]";
	return sizeStr;
}

int Query::setCacheSize(int requestedSize) {
	if (requestedSize < 0) {
		requestedSize = 0;
	} else if (requestedSize > 50) {
		requestedSize = 50;
	}
	pageCache.setCacheSize(requestedSize);
	return requestedSize;
}

char Query::to_hex(char c) {
	return c < 0xa ? '0' + c : 'a' - 0xa + c;
}

void Query::add_param(const char *param, pfc::string8 value, bool encode) {
	url << "&" << param << "=" << (encode ? url_encode(value) : value);
}

void Query::add_param(const char *param, int value) {
	pfc::string8 str;
	str << value;
	add_param(param, str, false);
}

void Query::add_param(const char *param, unsigned int value) {
	pfc::string8 str;
	str << value;
	add_param(param, str, false);
}

void Query::add_apikey() {
	add_param("api_key", lastfmApiKey);
}

pfc::string8 Query::url_encode(pfc::string8 in) {
	pfc::string8 out;
	out.prealloc(in.length() * 3 + 1);

	for (register const char *tmp = in; *tmp != '\0'; tmp++) {
		auto c = static_cast<unsigned char>(*tmp);
		if (isalnum(c) || c == '_') {
			out.add_char(c);
		} else if (isspace(c)) {
			out.add_char('+');
		} else {
			out.add_string("%25");
			out.add_char(to_hex(c >> 4));
			out.add_char(to_hex(c % 16));
		}
	}

	return out;
}

int hashCode(std::string text) {
	int hash = 0, strlen = text.length(), i;
	char character;
	if (strlen == 0)
		return hash;
	for (i = 0; i < strlen; i++) {
		character = text.at(i);
		hash = (31 * hash) + (character);
	}
	return hash;
}

pfc::string8 Query::perform(metadb_index_hash hash, abort_callback &callback) {
	static_api_ptr_t<http_client> http;
	bool cacheable = true;

	std::string buffer;
	CacheObj cacheVal;
	bool hit = pageCache.get(hashCode(url.get_ptr()), cacheVal);
	if (hit) {
		if (std::find(cacheVal.readHashes.begin(), cacheVal.readHashes.end(), hash) != cacheVal.readHashes.end()) {
			hit = false;					// this hash has read from this cache already, so clear cached value
			cacheVal.readHashes.clear();	// we'll retrieve a new response to cache and no hashes have read it yet
#ifdef DEBUG
			FB2K_console_formatter() << COMPONENT_NAME": This song has already read from this cached value, so re-querying";
#endif
		}
	}
	cacheVal.readHashes.push_back(hash);	// mark hash as having read from this url already
	if (!hit) {
		// cache miss so query api
		auto request = http->create_request("GET");
		request->add_header("User-Agent", COMPONENT_NAME "/" COMPONENT_VERSION);

		FB2K_console_formatter() << "Querying last.fm: " << url;
		file::ptr response;
		pfc::string8 buf;
		try {
			response = request->run_ex(url, callback);
			response->read_string_raw(buf, callback);
		} catch (...) {
			FB2K_console_formatter() << COMPONENT_NAME": Exception making call to last.fm. Returning empty response.";
			buf = "{}";
			cacheable = false;
		}

		cacheVal.response = buf.get_ptr();
		if (cacheable) {
			pageCache.set(hashCode(url.get_ptr()), cacheVal);
		}
	} else {
#ifdef DEBUG
		FB2K_console_formatter() << "Cache hit for: " << url;
#endif	
		pageCache.set(hashCode(url.get_ptr()), cacheVal);
	}

	return cacheVal.response.c_str();
}
