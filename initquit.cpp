#include "stdafx.h"
#include "globals.h"
#include "PlayedTimes.h"

// Sample initquit implementation. See also: initquit class documentation in relevant header.

class myinitquit : public initquit {
public:
	void on_init() {
		console::print(COMPONENT_NAME": on_init()");
		enhanced_playcount::convertHashes();
	}
	void on_quit() {
		console::print(COMPONENT_NAME": on_quit()");
	}
};

static initquit_factory_t<myinitquit> g_myinitquit_factory;
