#include "stdafx.h"
#include "resource.h"
#include "util.h"
#include "globals.h"
#include "PlaycountConfig.h"
#include "Bindings.h"

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
		COMMAND_HANDLER_EX(IDC_INCREMENT_WITH_PLAYCOUNT, BN_CLICKED, OnEditChange)
		COMMAND_HANDLER_EX(IDC_EPC_LASTFM_NAME, EN_CHANGE, OnEditChange)
		COMMAND_HANDLER_EX(IDC_LASTFM_CACHE_SIZE, EN_CHANGE, OnEditChange)
	END_MSG_MAP()

private:
	BOOL OnInitDialog(CWindow, LPARAM);
	void OnEditChange(UINT uNotifyCode, int nID, CWindow wndCtl);
	bool HasChanged() const;
	void OnChanged();
	void CreateTooltip(CToolTipCtrl, CWindow, int, LPCTSTR, LPCTSTR);

	preferences_page_callback::ptr const callback_;
	PlaycountConfig& config_;
	BindingCollection bindings_;
	CToolTipCtrl tooltips[2];
};

BOOL PlaycountPreferencesDialog::OnInitDialog(CWindow /*wndFocus*/, LPARAM /*lInitParam*/)
{
	bindings_.Bind(config_.EnableLastfmPlaycounts, m_hWnd, IDC_ENABLE_LASTFM_PLAYCOUNTS);
	bindings_.Bind(config_.IncrementLastfmWithPlaycount, m_hWnd, IDC_INCREMENT_WITH_PLAYCOUNT);
	bindings_.Bind(config_.LastfmUsername, m_hWnd, IDC_EPC_LASTFM_NAME);
	bindings_.Bind(config_.LruCacheSize, m_hWnd, IDC_LASTFM_CACHE_SIZE); 
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
	CreateTooltip(tooltips[1], m_hWnd, IDC_INCREMENT_WITH_PLAYCOUNT, 
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

	return FALSE;
}

void PlaycountPreferencesDialog::CreateTooltip(CToolTipCtrl tooltip, CWindow hWnd, int parent,
		LPCTSTR title, LPCTSTR body) {
	if (tooltip.Create(hWnd, nullptr, nullptr, TTS_NOPREFIX | TTS_BALLOON)) {
		CToolInfo toolInfo(TTF_IDISHWND | TTF_SUBCLASS | TTF_CENTERTIP,
			GetDlgItem(parent), 0, nullptr, nullptr);
		tooltip.AddTool(&toolInfo);

		tooltip.SetTitle(0, title);
		tooltip.UpdateTipText(body, GetDlgItem(parent));
		tooltip.SetMaxTipWidth(350);
		tooltip.SetDelayTime(TTDT_INITIAL, 250);
		tooltip.SetDelayTime(TTDT_AUTOPOP, 32000);
		tooltip.Activate(TRUE);
	}
}

void PlaycountPreferencesDialog::OnEditChange(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
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
	uSetDlgItemText(m_hWnd, IDC_EPC_LASTFM_NAME, DefaultLastfmUsername);
	uSetDlgItemText(m_hWnd, IDC_LASTFM_CACHE_SIZE, DefaultLruCacheSize);

	OnChanged();
}

void PlaycountPreferencesDialog::apply()
{
	bindings_.FlowToVar();
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