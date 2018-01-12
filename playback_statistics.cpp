#include "stdafx.h"

//class my_playback_statistics_collector : public playback_statistics_collector {
//
//public:
//	void on_item_played(metadb_handle_ptr p_item) {
//		file_info_impl p_info;
//		titleformat_object::ptr m_script;
//
//		if (m_script.is_empty()) {
//			pfc::string8 pattern;
//			static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(m_script, "%artist% %album% $if2(%discnumber%,1) %tracknumber% %title%");
//		}
//		pfc::string_formatter p_out;
//
//		//metadb_info_container::ptr info;
//		//if (!p_item->get_info_ref(info)) return;
//		//file_info &i = info->info();
//
//		p_item->format_title(NULL, p_out, m_script, NULL);
//		console::printf(p_out);
//	}
//};
//
//static playback_statistics_collector_factory_t<my_playback_statistics_collector> g_my_stat_collector;