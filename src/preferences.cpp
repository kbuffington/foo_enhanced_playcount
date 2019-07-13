#include "stdafx.h"
#include "resource.h"
#include "util.h"
#include "globals.h"
#include "PlaycountConfig.h"
#include "Bindings.h"
#include "Query.h"

namespace foo_enhanced_playcount {

namespace {

class PlaycountPreferencesDialog
	: public CDialogImpl<PlaycountPreferencesDialog>
	, public preferences_page_instance
{
public:
	PlaycountPreferencesDialog(preferences_page_callback::ptr callback)
		: callback_(callback)
		, config_(Config)
	{
	}

	static int const IDD = IDD_PREFERENCES;

	// #pragma region preferences_page_instance
	virtual t_uint32 get_state() override;
	virtual void apply() override;
	virtual void reset() override;
	// #pragma endregion

	BEGIN_MSG_MAP(PreferencesDialog)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDC_ENABLE_LASTFM_PLAYCOUNTS, BN_CLICKED, OnEditChange)
		COMMAND_HANDLER_EX(IDC_COMPARE_ALBUM_FIELDS, BN_CLICKED, OnEditChange)
		COMMAND_HANDLER_EX(IDC_INCREMENT_WITH_PLAYCOUNT, BN_CLICKED, OnEditChange)
		COMMAND_HANDLER_EX(IDC_REMOVE_DUPLICATE_SCROBBLES, BN_CLICKED, OnEditChange)
		COMMAND_HANDLER_EX(IDC_DELAY_PULLING_SCROBBLES, BN_CLICKED, OnEditChange)
		COMMAND_HANDLER_EX(IDC_EPC_LASTFM_NAME, EN_CHANGE, OnEditChange)
		COMMAND_HANDLER_EX(IDC_AUTO_PULL_SCROBBLES, BN_CLICKED, OnEditChange)
		COMMAND_HANDLER_EX(IDC_RESET_BUTTON, BN_CLICKED, OnClickedResetButton)
	END_MSG_MAP()


private:
	BOOL OnInitDialog(CWindow, LPARAM);
	void OnEditChange(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnClickedResetButton(UINT uNotifyCode, int nID, CWindow wndCtl);
	bool HasChanged() const;
	void OnChanged();
	void CreateTooltip(CToolTipCtrl, CWindow, int, LPCTSTR, LPCTSTR);

	preferences_page_callback::ptr const callback_;
	PlaycountConfig& config_;
	BindingCollection bindings_;
	CToolTipCtrl tooltips[6];
	Query *prefQ = new Query("fake");	// used for setting underlying cache
};

BOOL PlaycountPreferencesDialog::OnInitDialog(CWindow /*wndFocus*/, LPARAM /*lInitParam*/)
{
	bindings_.Bind(config_.EnableLastfmPlaycounts, m_hWnd, IDC_ENABLE_LASTFM_PLAYCOUNTS);
	bindings_.Bind(config_.CompareAlbumFields, m_hWnd, IDC_COMPARE_ALBUM_FIELDS); 
	bindings_.Bind(config_.IncrementLastfmWithPlaycount, m_hWnd, IDC_INCREMENT_WITH_PLAYCOUNT);
	bindings_.Bind(config_.RemoveDuplicateLastfmScrobbles, m_hWnd, IDC_REMOVE_DUPLICATE_SCROBBLES);
	bindings_.Bind(config_.delayScrobbleRetrieval, m_hWnd, IDC_DELAY_PULLING_SCROBBLES);
	bindings_.Bind(config_.LastfmUsername, m_hWnd, IDC_EPC_LASTFM_NAME);
	bindings_.Bind(config_.autoPullScrobbles, m_hWnd, IDC_AUTO_PULL_SCROBBLES);
	pfc::string8 str = "Next historical scrobble pull from: ";
	str << format_filetimestamp::format_filetimestamp(pfc::fileTimeUtoW(config_.earliestScrobbleChecked));
	GetDlgItem(IDC_LAST_PULL_DATE).SetWindowTextW(CA2W(str));
	bindings_.FlowToControl();

	CreateTooltip(tooltips[0], m_hWnd, IDC_ENABLE_LASTFM_PLAYCOUNTS,
		L"Retrieve scrobbles from last.fm",
		L"\nWhen this setting is enabled the component will attempt to retrieve last.fm "
		L"scrobbles for the currently playing track. This check will happen at the same time "
		L"the regular Playback Statistics component records a song is played: after 60 seconds "
		L"or the end of the track if at least 1/3rd of it has been played.\n\n"
		L"Last.fm does not allow retrieval of scrobbles by song title, they can only be retrieved "
		L"by artist. Because of this, the component will retrieve, at most, the last 1000 scrobbles "
		L"of the current artist. To determine if a scrobble matches the currently playing track, "
		L"a strict case-insensitive comparison is done matching %artist%, %album%, and %title%. "
		L"If any field is different, the scrobble will not be counted towards that track's total. "
		L"Last.fm has a tendency to record scrobbles as belonging to the wrong album title, or "
		L"appending \"(Disc 1)\" to the album title which will break the comparison check. It "
		L"is HIGHLY recommended that you use the foo_scrobble component instead of foo_audioscrobbler."
		L"\n\nRequires a valid last.fm username be set below."
	);
	CreateTooltip(tooltips[1], m_hWnd, IDC_COMPARE_ALBUM_FIELDS,
		L"Compare Album field when retrieving scrobbles",
		L"\nFor a scrobble to be counted as a play of this song, Artist, Title, and Album "
		L"must exactly match the values stored in Last.fm. Disabling this setting will only compare "
		L"Artist and Title against the scobbles that Last.fm has recorded. This will greatly increase "
		L"the chance of a match, but will pick up scrobbles from live albums, singles, etc., as long "
		L"the other fields are the same.\n\n"
		L"If you scrobble a lot from poorly tagged sources such as Spotify or Youtube you might want "
		L"to disable this setting."
	);
	CreateTooltip(tooltips[2], m_hWnd, IDC_INCREMENT_WITH_PLAYCOUNT, 
		L"Increment last.fm playcount with %play_count%",
		L"\nThe value of %play_count% automatically increments after 60 seconds of playtime, "
		L"while scrobbling the current song only happens after the song is played. Because this "
		L"component only automatically checks for last.fm plays at the same time %play_count% "
		L"increments the value of %lastfm_play_count% will always be 1 less than the value of "
		L"%play_count% if all scrobbles have happened from this instance foobar.\n\n"
		L"Leaving this checked will add 1 to the reported value of %lastfm_play_count% when "
		L"the last played time + 5mins is greater than the last scrobbled time and should "
		L"typically keep these two playcount values in sync.\n\n"
		L"Note: This setting only takes effect if the component has recorded at least one play "
		L"from Last.fm."
	);
	CreateTooltip(tooltips[3], m_hWnd, IDC_REMOVE_DUPLICATE_SCROBBLES,
		L"Remove duplicate scrobbles from Last.fm",
		L"\nLast.fm will sometimes report scrobbles of the same track with timestamps only "
		L"one second apart. These are typically caused by your scrobbling program sending "
		L"the same scrobble multiple times in error. However, scrobblers that only periodically "
		L"check for which songs have been played will usually only have the last played time "
		L"available. Therefore, multiple plays of the same song before connecting to the "
		L"scrobbler will be submitted with identical timestamps. This is commonly seen when "
		L"scrobbling plays from an iPhone/iPod using the desktop scrobbler for iTunes.\n\n"
		L"Uncheck if you want these extra scrobbles to be saved, and therefore returned "
		L"in %lastfm_played_times%, %lastfm_played_times_js%, and %lastfm_play_count%.\n\n"
		L"Note: This setting is applied at the time scrobbles are retrieved. Changes to this "
		L"setting are applied going forward unless you clear last.fm plays for the specified "
		L"tracks (right-click menu), and then re-retrieving them again."
	);
	CreateTooltip(tooltips[4], m_hWnd, IDC_DELAY_PULLING_SCROBBLES,
		L"Don't immediately pull scrobbles",
		L"\nTo avoid hammering Last.fm, and preventing the retrieval of scrobbles for songs you "
		L"aren't actually listening to, or that no longer exist in your library, it is recommended "
		L"to leave this option enabled. Scrobbles will be pulled after 2 seconds of playback."
	);
	CreateTooltip(tooltips[5], m_hWnd, IDC_AUTO_PULL_SCROBBLES,
		L"Automatically pull recent scrobbles",
		L"\nAt startup the component will retrieve a list of recently scrobbled songs (up to the "
		L"last 1000), and then search your fb2k library to find songs that match and attempt "
		L"to sync their last.fm playcounts with last.fm."
		L"\nThis option will also pull older scrobbles so that over time your entire library "
		L"will be brought up to date with last.fm."
	);

	return FALSE;
}

void PlaycountPreferencesDialog::CreateTooltip(CToolTipCtrl tooltip, CWindow hWnd, int parent,
		LPCTSTR title, LPCTSTR body) {
	if (tooltip.Create(hWnd, nullptr, nullptr, TTS_NOPREFIX | TTS_BALLOON)) {
		CToolInfo toolInfo(TTF_IDISHWND | TTF_SUBCLASS | TTF_CENTERTIP,
			GetDlgItem(parent), 0, nullptr, nullptr);
		HICON icon = 0;
		tooltip.AddTool(&toolInfo);

		tooltip.SetTitle(icon, title);
		tooltip.UpdateTipText(body, GetDlgItem(parent));
		tooltip.SetMaxTipWidth(350);
		tooltip.SetDelayTime(TTDT_INITIAL, 350);
		tooltip.SetDelayTime(TTDT_AUTOPOP, 32000);
		tooltip.Activate(TRUE);
	}
}

void PlaycountPreferencesDialog::OnEditChange(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	OnChanged();
}

t_uint32 PlaycountPreferencesDialog::get_state()
{
	t_uint32 state = preferences_state::resettable;
	if (HasChanged())
		state |= preferences_state::changed;
	return state;
}

void PlaycountPreferencesDialog::reset()
{
	CheckDlgButton(IDC_ENABLE_LASTFM_PLAYCOUNTS, BST_UNCHECKED);
	CheckDlgButton(IDC_INCREMENT_WITH_PLAYCOUNT, BST_CHECKED);
	CheckDlgButton(IDC_REMOVE_DUPLICATE_SCROBBLES, BST_CHECKED);
	CheckDlgButton(IDC_DELAY_PULLING_SCROBBLES, BST_CHECKED);
	CheckDlgButton(IDC_AUTO_PULL_SCROBBLES, BST_CHECKED);
	uSetDlgItemText(m_hWnd, IDC_EPC_LASTFM_NAME, DefaultLastfmUsername);

	OnChanged();
}

void PlaycountPreferencesDialog::OnClickedResetButton(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	config_.earliestScrobbleChecked = 0;
	pfc::string8 str = "Next historical scrobble pull from: ";
	str << format_filetimestamp::format_filetimestamp(pfc::fileTimeUtoW(config_.latestScrobbleChecked));
	GetDlgItem(IDC_LAST_PULL_DATE).SetWindowTextW(CA2W(str));

	OnChanged();
}

void PlaycountPreferencesDialog::apply()
{
	bindings_.FlowToVar();
	
	// Cache size used to be an option, now pinning at 50. Will be removed shortly
	prefQ->setCacheSize(50);
	
	OnChanged();

	PlaycountConfigNotify::NotifyChanged();
}

bool PlaycountPreferencesDialog::HasChanged() const
{
	return bindings_.HasChanged();
}

void PlaycountPreferencesDialog::OnChanged() { callback_->on_state_changed(); }

class PlaycountPreferencesPage : public preferences_page_impl<PlaycountPreferencesDialog>
{
public:
	virtual ~PlaycountPreferencesPage() = default;

	virtual const char* get_name() override { return "Enhanced Playcount"; }

	virtual GUID get_guid() override
	{
		// {BC1EA6C0-0731-4CEA-81AF-0CB211EE4C9D}
		static const GUID guid =
			{ 0xbc1ea6c0, 0x731, 0x4cea,{ 0x81, 0xaf, 0xc, 0xb2, 0x11, 0xee, 0x4c, 0x9d } };

		return guid;
	}

	virtual GUID get_parent_guid() override { return guid_tools; }
};

static preferences_page_factory_t<PlaycountPreferencesPage> g_PageFactory;

} // namespace
} // namespace foo_enhanced_playcount