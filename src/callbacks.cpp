#include "stdafx.h"
#include "globals.h"
#include "foobar2000/SDK/library_callbacks.h"
#include "foobar2000/SDK/system_time_keeper.h"
#include "PlayedTimes.h"

using namespace foo_enhanced_playcount;

class myLibraryCallbacks : public library_callback_v2
{
public:
	void on_library_initialized() final
	{
		FB2K_console_formatter() << "myLibrary init";
		// updateRecentScrobblesThreaded(true);
	}

	void on_items_added(metadb_handle_list_cref) final {}
	void on_items_modified(metadb_handle_list_cref) final {}
	void on_items_modified_v2(metadb_handle_list_cref, metadb_io_callback_v2_data &) final {}
	void on_items_removed(metadb_handle_list_cref) final {}
};

class myinitquit : public initquit
{
public:
	void on_init() final
	{
		console::print(COMPONENT_NAME ": loaded");
	}
	void on_quit() final
	{
		foobarQuitting();
		console::print(COMPONENT_NAME ": unloading");
	}
};

class myInitStageCallback : public init_stage_callback
{
public:
	void on_init_stage(t_uint32 stage)
	{
		switch (stage)
		{
		case init_stages::before_config_read:
			addMetadbIndexes();
			break;
		case init_stages::after_library_init:
			FB2K_console_formatter() << "myInitStageCallback init";
			updateRecentScrobblesThreaded(true);
			break;
		}
	}
};

FB2K_SERVICE_FACTORY(myLibraryCallbacks);
FB2K_SERVICE_FACTORY(myinitquit);
FB2K_SERVICE_FACTORY(myInitStageCallback);
