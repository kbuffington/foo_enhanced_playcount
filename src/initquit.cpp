#include "stdafx.h"
#include "globals.h"
#include "PlayedTimes.h"

class myinitquit : public initquit {
public:
	void on_init() {
		console::print(COMPONENT_NAME": loaded");
		foo_enhanced_playcount::convertHashes();
		foo_enhanced_playcount::updateRecentScrobblesThreaded(true);
	}
	void on_quit() {
		console::print(COMPONENT_NAME": unloading");
	}
};

static initquit_factory_t<myinitquit> g_myinitquit_factory;
