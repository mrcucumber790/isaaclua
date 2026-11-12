/* Daily runs without the Nintendo servers.
 *
 * Switch 1.7.9b keeps the daily challenge (Menu_DailyChallenge, Manager::StartDailyChallengeGame), but the menu
 * needs the NEX server: Menu_Game sets KAGE::Network::g_Manager+3 and asks to connect, Menu_DailyChallenge::Update
 * shows "connection lost" and closes while g_Manager+1 (connected) / +2 (logged in) are 0, and
 * KAGE::System::Time::ServerDateTimeNow() returns 1 (1970) when not logged in, so the seed would never change.
 *
 * Offline (and only offline) we:
 *  - make ServerDateTimeNow() return the console clock (nn::time user clock, UTC like the server time);
 *  - run Menu_DailyChallenge::Update with +1/+2 set and +3 cleared, so it goes the normal online way;
 *  - answer its leaderboard calls ourselves on the next frame: GetLeaderboard(LbFindCallback) ->
 *    LbScoreCallback(NULL, 0) ("not played today", state 2), UploadScore(LbUploadCallback) ->
 *    LbUploadCallback(true, true) (fade out and StartDailyChallengeGame, as after a real upload).
 *    The callbacks would be overwritten if called inside the game's call (Update stores state 1 right after it).
 *  - any other leaderboard request while faking (the leaderboard page) fails without touching the network.
 * Online nothing changes. Scores are not sent and the one-attempt-per-day check is not done offline. */

typedef bool (*DcGetLbFn)(void *, const char *, int, void *, void *);
typedef void (*DcUploadFn)(void *, void *, int, u8 *, void *, void *);
typedef long (*DcTimeFn)(void);
static VoidFn s_dc_update;
static DcGetLbFn s_dc_get_lb;
static DcUploadFn s_dc_upload;
static DcTimeFn s_dc_server_time;
static u8 *s_dc_net;
static void *s_dc_find_cb, *s_dc_upload_cb;
static void (*s_dc_score_cb)(void *, int, void *);
static void (*s_dc_upload_done)(bool, bool, int, int, void *);
static bool s_dc_faking;
static u8 s_dc_real[3];
static void *s_dc_pending_find, *s_dc_pending_upload;

static bool dc_real_online(void) {
    if (!s_dc_net) return true;
    return s_dc_faking ? (s_dc_real[0] && s_dc_real[1]) : (s_dc_net[1] && s_dc_net[2]);
}
static bool dc_real_logged_in(void) { return s_dc_faking ? s_dc_real[1] != 0 : s_dc_net && s_dc_net[2] != 0; }

static long dc_hk_server_time(void) {
    if (!s_dc_net || dc_real_logged_in()) return s_dc_server_time();
    long t = (long)isaaclua_now();
    return t > 1000000000L ? t : s_dc_server_time();
}

static bool dc_hk_get_lb(void *lm, const char *name, int sort, void *cb, void *ud) {
    if (!s_dc_faking) return s_dc_get_lb(lm, name, sort, cb, ud);
    if (cb == s_dc_find_cb && s_dc_score_cb) {
        s_dc_pending_find = ud;
        return true;
    }
    return false;
}

static void dc_hk_upload(void *lm, void *data, int score, u8 *extra, void *cb, void *ud) {
    if (!s_dc_faking) {
        s_dc_upload(lm, data, score, extra, cb, ud);
        return;
    }
    if (cb == s_dc_upload_cb && s_dc_upload_done) s_dc_pending_upload = ud;
}

static void dc_hk_update(void *menu) {
    if (!s_dc_net || dc_real_online()) {
        s_dc_update(menu);
        return;
    }
    for (int i = 0; i < 3; i++) s_dc_real[i] = s_dc_net[i + 1];
    s_dc_net[1] = 1;
    s_dc_net[2] = 1;
    s_dc_net[3] = 0;
    s_dc_faking = true;
    s_dc_update(menu);
    s_dc_faking = false;
    /* give the flags back unless the network thread changed them meanwhile */
    if (s_dc_net[1] == 1) s_dc_net[1] = s_dc_real[0];
    if (s_dc_net[2] == 1) s_dc_net[2] = s_dc_real[1];
    if (s_dc_net[3] == 0) s_dc_net[3] = s_dc_real[2];
    void *f = s_dc_pending_find, *u = s_dc_pending_upload;
    s_dc_pending_find = s_dc_pending_upload = NULL;
    if (f == menu) s_dc_score_cb(NULL, 0, menu);
    if (u == menu) s_dc_upload_done(true, true, 0, 0, menu);
}

static void daily_install(const ModView *v) {
#ifndef ISAACLUA_HOST_TEST
    s_dc_net = (u8 *)api_sym("_ZN4KAGE7Network9g_ManagerE");
    s_dc_find_cb =
        api_sym("_ZN15IsaacRepentance19Menu_DailyChallenge14LbFindCallbackEPN4KAGE6System15LeaderboardDataEPv");
    s_dc_upload_cb = api_sym("_ZN15IsaacRepentance19Menu_DailyChallenge16LbUploadCallbackEbbiiPv");
    s_dc_score_cb = (void (*)(void *, int, void *))api_sym(
        "_ZN15IsaacRepentance19Menu_DailyChallenge15LbScoreCallbackEPN4KAGE6System16LeaderboardEntryEiPv");
    s_dc_upload_done = (void (*)(bool, bool, int, int, void *))s_dc_upload_cb;
    if (!s_dc_net || !s_dc_find_cb || !s_dc_upload_cb || !s_dc_score_cb) {
        s_dc_net = NULL;
        plugin_log("[daily] symbols missing, daily runs need the server");
        return;
    }
    s_dc_server_time = (DcTimeFn)hook_got(v, "_ZN4KAGE6System4Time17ServerDateTimeNowEv", (void *)dc_hk_server_time);
    s_dc_get_lb = (DcGetLbFn)hook_got(
        v,
        "_ZN4KAGE6System18LeaderboardManager14GetLeaderboardEPKcNS0_22LeaderboardManagerBase9eSortModeEPFvPNS0_"
        "15LeaderboardDataEPvES8_",
        (void *)dc_hk_get_lb);
    s_dc_upload = (DcUploadFn)hook_got(
        v, "_ZN4KAGE6System18LeaderboardManager11UploadScoreEPNS0_15LeaderboardDataEiPhPFvbbiiPvES5_",
        (void *)dc_hk_upload);
    s_dc_update = (VoidFn)hook_got(v, "_ZN15IsaacRepentance19Menu_DailyChallenge6UpdateEv", (void *)dc_hk_update);
    if (!s_dc_server_time || !s_dc_get_lb || !s_dc_upload || !s_dc_update) {
        plugin_log("[daily] GOT slots not found, offline daily runs off");
        s_dc_net = NULL; /* hooks that did install pass everything through */
        return;
    }
    plugin_log("[daily] offline daily runs on");
#else
    (void)v;
#endif
}
