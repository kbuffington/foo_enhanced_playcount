#include "stdafx.h"
#include "globals.h"
#include "Query.h"
#include "LruCache.cpp"
#include "PlaycountConfig.h"

static const pfc::string8 lastfmApiKey = "a1685abe5265b93cf2be4a70d181bf6b";

// by artist
// http://ws.audioscrobbler.com/2.0/?method=user.getartisttracks&user=MordredKLB&artist=metallica&api_key=a1685abe5265b93cf2be4a70d181bf6b&format=json

// by song
// http://ws.audioscrobbler.com/2.0/?method=user.getTrackScrobbles&username=MordredKLB&artist=metallica&track=whiplash&api_key=a1685abe5265b93cf2be4a70d181bf6b&format=json

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

void Query::add_param(const char *param, pfc::string8 value) {
	url << "&" << param << "=" << url_encode(value);
}

void Query::add_param(const char *param, int value) {
	pfc::string8 str;
	str << value;
	add_param(param, str);
}

void Query::add_param(const char *param, unsigned int value) {
	pfc::string8 str;
	str << value;
	add_param(param, str);
}

void Query::add_param(const char* param, t_filetimestamp value) {
	pfc::string8 str;
	str << value;
	add_param(param, str);
}

void Query::add_apikey() {
	add_param("api_key", lastfmApiKey);
}

pfc::string8 Query::url_encode(pfc::string8 in) {
	in.replace_string("%", "%25", 0);	// escape % first, otherwise we'll be double escaping # or &
	in.replace_string("#", "%23", 0);
	in.replace_string("&", "%26", 0);
	in.replace_string("+", "%2B", 0);

	return in;
}

int hashCode(std::string text) {
	int hash = 0, strlen = (int) text.length(), i;
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
		}
		catch (const std::exception & e) {
			FB2K_console_formatter() << COMPONENT_NAME": Exception making call to last.fm: " << e.what();
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
