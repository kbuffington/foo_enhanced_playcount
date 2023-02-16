#include "stdafx.h"
#include "globals.h"
#include "foobar2000/SDK/library_callbacks.h"
#include "PlayedTimes.h"

class myLibraryCallbacks : public library_callback_v2 {
public:
	void on_library_initialized() final {
		foo_enhanced_playcount::updateRecentScrobblesThreaded(true);
	}

	void on_items_added(metadb_handle_list_cref) final {}
	void on_items_modified(metadb_handle_list_cref) final {}
	void on_items_modified_v2(metadb_handle_list_cref, metadb_io_callback_v2_data&) final {}
	void on_items_removed(metadb_handle_list_cref) final {}
};

class myinitquit : public initquit {
public:
	void on_init() final {
		console::print(COMPONENT_NAME": loaded");
		if (!core_version_info_v2::get()->test_version(2, 0, 0, 0))
		{
			foo_enhanced_playcount::updateRecentScrobblesThreaded(true);
		}
	}
	void on_quit() final {
		console::print(COMPONENT_NAME": unloading");
	}
};

FB2K_SERVICE_FACTORY(myLibraryCallbacks);
FB2K_SERVICE_FACTORY(myinitquit);
