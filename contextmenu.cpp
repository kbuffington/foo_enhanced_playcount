#include "stdafx.h"


// Identifier of our context menu group.
// {B09794FD-65C0-4C1D-99EB-1A5674C90AD3}
static const GUID context_menu_guid = { 0xb09794fd, 0x65c0, 0x4c1d,{ 0x99, 0xeb, 0x1a, 0x56, 0x74, 0xc9, 0xa, 0xd3 } };


// Switch to contextmenu_group_factory to embed your commands in the root menu but separated from other commands.

//static contextmenu_group_factory g_mygroup(guid_mygroup, contextmenu_groups::root, 0);
static contextmenu_group_popup_factory g_mygroup(context_menu_guid, contextmenu_groups::root, "Enhanced Playcount", 0);

//static void RunTestCommand(metadb_handle_list_cref data);
void ClearLastFmRecords(metadb_handle_list_cref items); //played_times.cpp
void GetLastfmScrobblesThreaded(metadb_handle_list_cref items); //played_times.cpp

// Simple context menu item class.
class myitem : public contextmenu_item_simple {
public:
	enum {
		cmd_clear_lastfm = 0,
		cmd_get_scrobbles,
		cmd_total
	};
	GUID get_parent() {return context_menu_guid;}
	unsigned get_num_items() {return cmd_total;}
	void get_item_name(unsigned p_index,pfc::string_base & p_out) {
		switch(p_index) {
			case cmd_clear_lastfm: p_out = "Clear saved last.fm plays"; break;
			case cmd_get_scrobbles: p_out = "Get last.fm scrobbles (max 50 tracks)"; break;
			default: uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}
	}
	void context_command(unsigned p_index,metadb_handle_list_cref p_data,const GUID& p_caller) {
		switch(p_index) {
			case cmd_clear_lastfm:
				ClearLastFmRecords(p_data);
				break;
			case cmd_get_scrobbles:
				GetLastfmScrobblesThreaded(p_data);
				break;
			default:
				uBugCheck();
		}
	}
	// Overriding this is not mandatory. We're overriding it just to demonstrate stuff that you can do such as context-sensitive menu item labels.
	//bool context_get_display(unsigned p_index,metadb_handle_list_cref p_data,pfc::string_base & p_out,unsigned & p_displayflags,const GUID & p_caller) {
	//	switch(p_index) {
	//		case cmd_clear_lastfm:
	//			if (!__super::context_get_display(p_index, p_data, p_out, p_displayflags, p_caller)) return false;
	//			// Example context sensitive label: append the count of selected items to the label.
	//			p_out << " : " << p_data.get_count() << " item";
	//			if (p_data.get_count() != 1) p_out << "s";
	//			p_out << " selected";
	//			return true;
	//		default: uBugCheck();
	//	}
	//}
	GUID get_item_guid(unsigned p_index) {
		// These GUIDs identify our context menu items.
		static const GUID guid_reset = { 0xac78cb04, 0x1f14, 0x4fcf,{ 0x83, 0xeb, 0x88, 0xb1, 0x9b, 0x3c, 0xe2, 0x31 } };
		static const GUID guid_get_scrobbles = { 0xa3c55d95, 0x64f9, 0x4349,{ 0xa1, 0x2f, 0x62, 0xea, 0x7f, 0xb4, 0x81, 0x2a } };


		switch(p_index) {
			case cmd_clear_lastfm: return guid_reset;
			case cmd_get_scrobbles: return guid_get_scrobbles;
			default: uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}

	}
	bool get_item_description(unsigned p_index,pfc::string_base & p_out) {
		switch(p_index) {
			case cmd_clear_lastfm:
				p_out = "Clears all recorded last.fm plays for selected tracks.";
				return true;
			case cmd_get_scrobbles:
				p_out = "Pulls list of scrobbles from Last.fm for up to 50 tracks.";
				return true;
			default:
				uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}
	}
};

static contextmenu_item_factory_t<myitem> g_myitem_factory;


//static void RunTestCommand(metadb_handle_list_cref data) {
//	pfc::string_formatter message;
//	message << "This is a test command.\n";
//	if (data.get_count() > 0) {
//		message << "Parameters:\n";
//		for(t_size walk = 0; walk < data.get_count(); ++walk) {
//			message << data[walk] << "\n";
//		}
//	}	
//	popup_message::g_show(message, "Blah");
//}
