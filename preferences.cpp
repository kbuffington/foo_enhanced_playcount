#include "stdafx.h"
#include "resource.h"
#include "util.h"
#include "globals.h"


// These GUIDs identify the variables within our component's configuration file.
static const GUID guid_cfg_lastfm_name = { 0xbd5c777, 0x735c, 0x440d, { 0x8c, 0x71, 0x49, 0xb6, 0xac, 0xff, 0xce, 0xb8 } };
static const GUID guid_cfg_bogoSetting2 = { 0x752f1186, 0x9f61, 0x4f91, { 0xb3, 0xee, 0x2f, 0x25, 0xb1, 0x24, 0x83, 0x5d } };

// This GUID identifies our Advanced Preferences branch
static const GUID guid_advconfig_branch = { 0xb6d0b26b, 0x7988, 0x4f5f, { 0x8c, 0x2c, 0x84, 0xa0, 0x21, 0x80, 0xf8, 0xf4 } };

// This GUID identifies our Advanced Preferences setting as well as this setting's storage within our component's configuration file.
static const GUID guid_cfg_bogoSetting3 = { 0x2838e7ed, 0x138f, 0x4198, { 0xba, 0x85, 0xc2, 0xc5, 0xbe, 0xdb, 0xca, 0xa1 } };


enum {
	default_cfg_bogoSetting2 = 666,
	default_cfg_bogoSetting3 = 42,
};

const LPCTSTR default_cfg_lastfm_name = L"<none>";


static cfg_string cfg_lastfm_name(guid_cfg_lastfm_name, reinterpret_cast<const char *>(default_cfg_lastfm_name));
static cfg_uint cfg_bogoSetting2(guid_cfg_bogoSetting2, default_cfg_bogoSetting2);

char g_lastfm_username[256];

//static advconfig_branch_factory g_advconfigBranch("Enhanced Playcount", guid_advconfig_branch, advconfig_branch::guid_branch_tools, 0);
//static advconfig_integer_factory cfg_bogoSetting3("Bogo setting 3", guid_cfg_bogoSetting3, guid_advconfig_branch, 0, default_cfg_bogoSetting3, 0 /*minimum value*/, 9999 /*maximum value*/);

class CMyPreferences : public CDialogImpl<CMyPreferences>, public preferences_page_instance {
public:
	//Constructor - invoked by preferences_page_impl helpers - don't do Create() in here, preferences_page_impl does this for us
	CMyPreferences(preferences_page_callback::ptr callback) : m_callback(callback) {}

	//Note that we don't bother doing anything regarding destruction of our class.
	//The host ensures that our dialog is destroyed first, then the last reference to our preferences_page_instance object is released, causing our object to be deleted.


	//dialog resource ID
	enum {IDD = IDD_MYPREFERENCES};
	// preferences_page_instance methods (not all of them - get_wnd() is supplied by preferences_page_impl helpers)
	t_uint32 get_state();
	void apply();
	void reset();

	//WTL message map
	BEGIN_MSG_MAP(CMyPreferences)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDC_EPC_LASTFM_NAME, EN_CHANGE, OnEditChange)
		//COMMAND_HANDLER_EX(IDC_BOGO2, EN_CHANGE, OnEditChange)
	END_MSG_MAP()
private:
	BOOL OnInitDialog(CWindow, LPARAM);
	void OnEditChange(UINT, int, CWindow);
	bool HasChanged();
	void OnChanged();

	const preferences_page_callback::ptr m_callback;
};

BOOL CMyPreferences::OnInitDialog(CWindow, LPARAM) {
	std::wstring username = util::str2wstr(cfg_lastfm_name.get_ptr());
	SetDlgItemText(IDC_EPC_LASTFM_NAME, username.c_str());
	strncpy_s(g_lastfm_username, cfg_lastfm_name.get_ptr(), strlen(cfg_lastfm_name.get_ptr()));
	//SetDlgItemInt(IDC_BOGO2, cfg_bogoSetting2, FALSE);
	return FALSE;
}

void CMyPreferences::OnEditChange(UINT, int, CWindow) {
	// not much to do here
	OnChanged();
}

t_uint32 CMyPreferences::get_state() {
	t_uint32 state = preferences_state::resettable;
	if (HasChanged()) state |= preferences_state::changed;
	return state;
}

void CMyPreferences::reset() {
	SetDlgItemText(IDC_EPC_LASTFM_NAME, default_cfg_lastfm_name);
	//SetDlgItemInt(IDC_BOGO2, default_cfg_bogoSetting2, FALSE);
	OnChanged();
}

void CMyPreferences::apply() {
	wchar_t wstr[1024];

	// convert filenames to char
	GetDlgItemText(IDC_EPC_LASTFM_NAME, (LPTSTR)wstr, sizeof(wstr));
	auto nameStr = (util::wstr2str(wstr)).c_str();
	cfg_lastfm_name.set_string(nameStr);
	strncpy_s(g_lastfm_username, nameStr, strlen(nameStr));

	cfg_bogoSetting2 = GetDlgItemInt(IDC_BOGO2, NULL, FALSE);
	
	OnChanged(); //our dialog content has not changed but the flags have - our currently shown values now match the settings so the apply button can be disabled
}

bool CMyPreferences::HasChanged() {
	//returns whether our dialog content is different from the current configuration (whether the apply button should be enabled or not)
	wchar_t wstr[1024];
	bool has_changed;

	GetDlgItemText(IDC_EPC_LASTFM_NAME, (LPTSTR)wstr, sizeof(wstr));
	has_changed = wcscmp(wstr, (util::str2wstr(cfg_lastfm_name.get_ptr())).c_str()) != 0;

	return has_changed ||
			GetDlgItemInt(IDC_BOGO2, NULL, FALSE) != cfg_bogoSetting2;
}
void CMyPreferences::OnChanged() {
	//tell the host that our state has changed to enable/disable the apply button appropriately.
	m_callback->on_state_changed();
}

class preferences_page_myimpl : public preferences_page_impl<CMyPreferences> {
	// preferences_page_impl<> helper deals with instantiation of our dialog; inherits from preferences_page_v3.
public:
	const char * get_name() {return "Enhanced Playcount";}
	GUID get_guid() {
		FB2K_console_formatter() << "prefs loaded!";
		static const GUID guid = { 0xbc5c0e0a, 0x36ea, 0x4947, { 0x83, 0x54, 0xb, 0x42, 0x93, 0x45, 0xd1, 0xd9 } };

		return guid;
	}
	GUID get_parent_guid() {return guid_tools;}
};

static preferences_page_factory_t<preferences_page_myimpl> g_preferences_page_myimpl_factory;
