#include "stdafx.h"
#include "util.h"
#include "PlaycountConfig.h"
#include <sstream>
#include "PlayedTimes.h"

using namespace foo_enhanced_playcount;

extern PlaycountConfig const& config;

namespace enhanced_playcount_fields {

	std::string getPlayTimesStr(std::vector<t_filetimestamp> playTimes, bool convertTimeStamp, bool jsTimeStamp);

#define kNoDate 199999999990000000

	enum provided_fields {
		PLAYED_TIMES,
		PLAYED_TIMES_JS,
		PLAYED_TIMES_RAW,

		LASTFM_PLAYED_TIMES,
		LASTFM_PLAYED_TIMES_JS,
		LASTFM_PLAY_COUNT,
		LASTFM_ADDED,
		LASTFM_FIRST_PLAYED,
		LASTFM_LAST_PLAYED,

		FIRST_PLAYED_ENHANCED,
		LAST_PLAYED_ENHANCED,
		ADDED_ENHANCED,

		MAX_NUM_FIELDS	// always last entry in this enum
	};

	static std::vector<t_filetimestamp> playtimes_get(metadb_index_hash hash, bool last_fm_times) {
		std::vector<t_filetimestamp> playTimes;
		record_t record = getRecord(hash);

		if (last_fm_times) {
			return record.lastfmPlaytimes;
		}
		else {
			return record.foobarPlaytimes;
		}

		return playTimes;
	}

	static int playcount_get(metadb_index_hash hash, bool last_fm_times) {
		record_t record = getRecord(hash);
		int count = record.numLastfmPlays;

		if (config.IncrementLastfmWithPlaycount && record.numFoobarPlays && record.numLastfmPlays) {
			t_filetimestamp lastRecordedTime;
			if (record.numLastfmPlays) {
				std::vector<t_filetimestamp>::reverse_iterator it = record.lastfmPlaytimes.rbegin();
				lastRecordedTime = *it;
			}

			std::vector<t_filetimestamp>::reverse_iterator rit = record.foobarPlaytimes.rbegin();
			if (*rit > lastRecordedTime + 300 * filetimestamp_1second_increment) { // > lastRecordedTime + 5mins
				count++;
			}
		}
		return count;
	}

	titleformat_object::ptr first_played_script;
	titleformat_object::ptr last_played_script;
	titleformat_object::ptr date_added_script;

	class metadb_display_field_provider_impl : public metadb_display_field_provider {
	public:
		t_uint32 get_field_count() {
			return MAX_NUM_FIELDS;
		}
		void get_field_name(t_uint32 index, pfc::string_base& out) {
			PFC_ASSERT(index >= 0 && index < MAX_NUM_FIELDS);
			switch (index) {
			case PLAYED_TIMES:
				out = "played_times";
				break;
			case PLAYED_TIMES_JS:
				out = "played_times_js";
				break;
			case PLAYED_TIMES_RAW:
				out = "played_times_raw";
				break;
			case LASTFM_PLAYED_TIMES:
				out = "lastfm_played_times";
				break;
			case LASTFM_PLAYED_TIMES_JS:
				out = "lastfm_played_times_js";
				break;
			case LASTFM_PLAY_COUNT:
				out = "lastfm_play_count";
				break;
			case LASTFM_ADDED:
				out = "lastfm_added";
				break;
			case LASTFM_FIRST_PLAYED:
				out = "lastfm_first_played";
				break;
			case LASTFM_LAST_PLAYED:
				out = "lastfm_last_played";
				break;
			case FIRST_PLAYED_ENHANCED:
				out = "first_played_enhanced";
				break;
			case LAST_PLAYED_ENHANCED:
				out = "last_played_enhanced";
				break;
			case ADDED_ENHANCED:
				out = "added_enhanced";
				break;
			}
		}
		bool process_field(t_uint32 index, metadb_handle* handle, titleformat_text_out* out) {
			PFC_ASSERT(index >= 0 && index < MAX_NUM_FIELDS);
			metadb_index_hash hash;
			if (!clientByGUID(guid_foo_enhanced_playcount_index)->hashHandle(handle, hash)) return false;
			std::vector<t_filetimestamp> playTimes, lastfmPlayTimes;
			t_filetimestamp fbTime = 0, lastfmTime = 0, firstPlayed = 0, lastPlayed = 0;
			file_info_impl info;
			unsigned int count;

			switch (index) {
			case PLAYED_TIMES:
			case PLAYED_TIMES_JS:
			case PLAYED_TIMES_RAW:
				playTimes = playtimes_get(hash, false);
				if (!playTimes.size()) {
					out->write(titleformat_inputtypes::meta, "[]");
				}
				else {
					switch (index) {
					case PLAYED_TIMES:
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, true, false).c_str());
						break;
					case PLAYED_TIMES_JS:
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, false, true).c_str());
						break;
					case PLAYED_TIMES_RAW:
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, false, false).c_str());
						break;
					}
				}
				break;
			case LASTFM_PLAYED_TIMES:
			case LASTFM_PLAYED_TIMES_JS:
				playTimes = playtimes_get(hash, true);
				if (!playTimes.size()) {
					out->write(titleformat_inputtypes::meta, "[]");
				}
				else {
					if (index == LASTFM_PLAYED_TIMES) {
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, true, false).c_str());
					}
					else {
						out->write(titleformat_inputtypes::meta, getPlayTimesStr(playTimes, false, true).c_str());
					}
				}
				break;
			case LASTFM_PLAY_COUNT:
				count = playcount_get(hash, true);
				out->write_int(titleformat_inputtypes::meta, count);
				if (count == 0) {
					return false;
				}
				break;
			case LASTFM_ADDED:
			case LASTFM_FIRST_PLAYED:
				playTimes = playtimes_get(hash, true);
				if (!playTimes.size()) {
					out->write(titleformat_inputtypes::meta, "N/A");
					return false;
				}
				else {
					out->write(titleformat_inputtypes::meta,
						format_filetimestamp::format_filetimestamp(playTimes.front()));
				}
				break;
			case LASTFM_LAST_PLAYED:
				playTimes = playtimes_get(hash, true);
				if (!playTimes.size()) {
					out->write(titleformat_inputtypes::meta, "N/A");
					return false;
				}
				else {
					out->write(titleformat_inputtypes::meta,
						format_filetimestamp::format_filetimestamp(playTimes.back()));
				}
				break;
			case FIRST_PLAYED_ENHANCED:
				playTimes = playtimes_get(hash, false);
				lastfmPlayTimes = playtimes_get(hash, true);
				fbTime = lastfmTime = kNoDate;
				if (playTimes.size()) {
					fbTime = playTimes.front();
				}
				if (lastfmPlayTimes.size()) {
					lastfmTime = lastfmPlayTimes.front();
				}
				if (fbTime < lastfmTime) {
					firstPlayed = fbTime;
				}
				else {
					firstPlayed = lastfmTime;
				}
				if (firstPlayed != kNoDate) {
					out->write(titleformat_inputtypes::meta,
						format_filetimestamp::format_filetimestamp(firstPlayed));
				}
				else {
					if (first_played_script.is_empty()) {
						static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(first_played_script, "%first_played%");
					}
					pfc::string_formatter p_out;
					handle->format_title(NULL, p_out, first_played_script, NULL);

					if (strcmp(p_out.toString(), "N/A")) {
						t_filetimestamp first_played = foobar2000_io::filetimestamp_from_string(p_out);
						out->write(titleformat_inputtypes::meta,
							format_filetimestamp::format_filetimestamp(first_played));
					}
					else {
						out->write(titleformat_inputtypes::meta, "N/A");
						return false;
					}
				}
				break;
			case LAST_PLAYED_ENHANCED:
				playTimes = playtimes_get(hash, false);
				lastfmPlayTimes = playtimes_get(hash, true);
				if (playTimes.size()) {
					fbTime = playTimes.back();
				}
				if (lastfmPlayTimes.size()) {
					lastfmTime = lastfmPlayTimes.back();
				}
				if (fbTime > lastfmTime) {
					lastPlayed = fbTime;
				}
				else {
					lastPlayed = lastfmTime;
				}
				if (lastPlayed) {
					out->write(titleformat_inputtypes::meta,
						format_filetimestamp::format_filetimestamp(lastPlayed));
				}
				else {
					if (last_played_script.is_empty()) {
						static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(last_played_script, "%last_played%");
					}
					pfc::string_formatter p_out;
					handle->format_title(NULL, p_out, last_played_script, NULL);

					if (strcmp(p_out.toString(), "N/A")) {
						t_filetimestamp last_played = foobar2000_io::filetimestamp_from_string(p_out);
						out->write(titleformat_inputtypes::meta,
							format_filetimestamp::format_filetimestamp(last_played));
					}
					else {
						out->write(titleformat_inputtypes::meta, "N/A");
						return false;
					}
				}
				break;
			case ADDED_ENHANCED:
				if (date_added_script.is_empty()) {
					static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(date_added_script, "%added%");
				}
				pfc::string_formatter p_out;

				handle->format_title(NULL, p_out, date_added_script, NULL);

				if (strcmp(p_out.toString(), "N/A")) {
					t_filetimestamp added = foobar2000_io::filetimestamp_from_string(p_out);

					lastfmPlayTimes = playtimes_get(hash, true);
					if (lastfmPlayTimes.size() && lastfmPlayTimes.front() < added) {
						out->write(titleformat_inputtypes::meta,
							format_filetimestamp::format_filetimestamp(lastfmPlayTimes.front()));
					}
					else {
						out->write(titleformat_inputtypes::meta,
							format_filetimestamp::format_filetimestamp(added));
					}
				}
				else {
					return false;	// can we get here?
				}
				break;
			}
			return true;
		}
	};

	static service_factory_single_t<metadb_display_field_provider_impl> g_metadb_display_field_provider_impl;

	std::string t_uint64_to_string(t_uint64 value, bool jsTimestamp) {
		std::ostringstream os;
		if (jsTimestamp) {
			t_uint64 jsValue = util::timestampWindowsToJS(value);	// convert to unix timestamp, then add milliseconds for JS
			os << jsValue;
		}
		else {
			os << value;
		}
		return os.str();
	}

	static std::string getPlayTimesStr(std::vector<t_filetimestamp> playTimes, bool convertTimeStamp, bool jsTimeStamp) {
		std::string str;
		size_t index = 0;

		str += "[";
		for (std::vector<t_filetimestamp>::iterator it = playTimes.begin(); it != playTimes.end(); ++it, ++index) {
			if (convertTimeStamp) {
				str += "\"";
				str.append(format_filetimestamp::format_filetimestamp(*it));
				str += "\"";
			}
			else {
				str += t_uint64_to_string(*it, jsTimeStamp);
			}
			if (index + 1 < playTimes.size()) {
				str.append(", ");
			}
		}
		str += "]";
		return str;
	}
}