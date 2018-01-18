#include "stdafx.h"
#include "globals.h"
#include "query.h"
#include "LruCache.cpp"

static const pfc::string8 lastfmApiKey = "a1685abe5265b93cf2be4a70d181bf6b";

// http://ws.audioscrobbler.com/2.0/?method=user.getartisttracks&user=MordredKLB&artist=metallica&api_key=a1685abe5265b93cf2be4a70d181bf6b&format=json

static const char lastfmApiBase[] = "http://ws.audioscrobbler.com/2.0/?method=";


using namespace foo_enhanced_playcount;
using namespace pfc;

LruCache<int, std::string> pageCache(10);	// couldn't figure out how to create this in the constructor

Query::Query(const char *method) {
	url << lastfmApiBase << method;
}

char Query::to_hex(char c) {
	return c < 0xa ? '0' + c : 'a' - 0xa + c;
}

//void Query::add_param(const char *param, const char *value, bool encode) {
//	url << "&" << param << "=" << (encode ? url_encode(value) : value);
//}

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
		if (isalnum(c)) {
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

int  hashCode(std::string text) {
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

pfc::string8 Query::perform(abort_callback &callback) {
	// Download
	static_api_ptr_t<http_client> http;
	bool cacheable = true;
	auto request = http->create_request("GET");

	request->add_header("User-Agent", "foo_enhanced_playcount/" COMPONENT_VERSION);

	std::string buffer = url.get_ptr();
	if (strstr(url.toString(), "startTimestamp=")) {
		// we can't cache lastFm plays with a timestamp, because we mark a playcount before last.fm does and we'd never get updated values until the cache entry expires
		cacheable = false;
	}
	if (!cacheable || !pageCache.get(hashCode(url.get_ptr()), buffer)) {
//#ifdef DEBUG		// TODO: put this back
		FB2K_console_formatter() << "Calling last.fm API: " << url;
//#endif	
		// cach miss so query api
		auto response = request->run_ex(url, callback);

		// Get string
		pfc::string8 buf;
		response->read_string_raw(buf, callback);

		buffer = buf.get_ptr();
		if (cacheable) {
			pageCache.set(hashCode(url.get_ptr()), buffer);
		}
	} else {
//#ifdef DEBUG
		FB2K_console_formatter() << "Cache hit for: " << url;
//#endif	
	}

	return buffer.c_str();
}
