#include "stdafx.h"
#include "globals.h"
#include "foobar2000/SDK/library_callbacks.h"
#include "PlayedTimes.h"


class myLibraryCallbacks : public library_callback_v2_dynamic_impl_base {
public:
	void on_library_initialized() {
		foo_enhanced_playcount::updateRecentScrobblesThreaded(true);
	}
};

class myinitquit : public initquit {
public:
	void on_init() {
		console::print(COMPONENT_NAME": loaded");
		// foo_enhanced_playcount::convertHashes();
		new myLibraryCallbacks();
	}
	void on_quit() {
		console::print(COMPONENT_NAME": unloading");
	}
};

static initquit_factory_t<myinitquit> g_myinitquit_factory;
