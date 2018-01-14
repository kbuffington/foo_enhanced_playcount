#include "stdafx.h"
#include "globals.h"
#include "query.h"

static const pfc::string8 lastfmApiKey = "a1685abe5265b93cf2be4a70d181bf6b";

// http://ws.audioscrobbler.com/2.0/?method=user.getartisttracks&user=MordredKLB&artist=metallica&api_key=a1685abe5265b93cf2be4a70d181bf6b&format=json

static const char lastfmApiBase[] = "http://ws.audioscrobbler.com/2.0/?method=";


using namespace foo_enhanced_playcount;

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

void Query::add_apikey() {
	add_param("api_key", lastfmApiKey);
}

pfc::string8 Query::url_encode(pfc::string8 in, bool encodeSpecialChars) {
	pfc::string8 out;
	out.prealloc(in.length() * 3 + 1);
	//out.prealloc(strlen(in) * 3 + 1);

	if (encodeSpecialChars) {

		for (register const char *tmp = in; *tmp != '\0'; tmp++) {
			auto c = static_cast<unsigned char>(*tmp);
			if (isalnum(c)) {
				out.add_char(c);
			} else if (isspace(c)) {
				out.add_char('+');
			} else {
				out.add_char('%');
				out.add_char(to_hex(c >> 4));
				out.add_char(to_hex(c % 16));
			}
		}
	} else {
		out << in;
		out.replace_char(' ', '+', 0);
		out.replace_string("&", "%2526", 0);
	}

	return out;
}

pfc::string8 Query::perform(abort_callback &callback) {
#ifdef DEBUG
	auto logger = uDebugLog();
	logger << "MusicBrainz tagger: accessing " << url;
#endif

	// Download
	static_api_ptr_t<http_client> http;
	auto request = http->create_request("GET");
	request->add_header("User-Agent", "foo_musicbrainz/" COMPONENT_VERSION);
#ifdef DEBUG
	FB2K_console_formatter() << "Calling last.fm API with: " << url;
	//auto buffer = "";
#else
#endif	
	auto response = request->run_ex(url, callback);
	
	// Get string
	pfc::string8 buffer;
	response->read_string_raw(buffer, callback);

	return buffer;
}
