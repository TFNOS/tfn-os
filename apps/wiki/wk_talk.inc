// WIKI: THE TALK KEY (tfn/listen.h). "look up Paris", "tell me about the moon", "who was Albert Einstein", "what is a
// black hole": the topic goes to the search of the open file. A title that is the same opens at once, else the list
// of the nearest titles shows. Only with an English file (the ears hear English); else TFN OS shows its answer.
static const char *const WT_ASK[] = {"look up ",           "search for ",       "search ",     "find ",
                                     "tell me about ",     "what is a ",        "what is an ", "what is the ",
                                     "what is ",           "what are ",         "who is ",     "who was ",
                                     "who were ",          "show me ",          "read about ", "wikipedia ",
                                     "the article about ", "an article about ", NULL};

// the words in the form of a title: a capital at each word, or only at the first word
static void wt_title(const char *in, char *out, int cap, bool each)
{
    int n = 0;
    bool start = true;
    for (const char *p = in; *p && n < cap - 1; p++) {
        char c = *p;
        if (start && c >= 'a' && c <= 'z' && (each || n == 0)) {
            c = (char)(c - 32);
        }
        start = c == ' ';
        out[n++] = c;
    }
    out[n] = 0;
}

static bool wt_heard(const char *heard, const char *answer)
{
    (void)answer;
    if (!s_z || s_zlang == 1 || s_sc == SC_NOFILE) {
        return false;
    }
    const char *topic = NULL;
    for (int i = 0; WT_ASK[i] && !topic; i++) {
        const char *p = strstr(heard, WT_ASK[i]);
        if (p) {
            topic = p + strlen(WT_ASK[i]);
        }
    }
    if (!topic || !topic[0]) {
        return false;
    }
    char t[64];
    snprintf(t, sizeof t, "%s", topic);
    const size_t l = strlen(t);
    if (l > 7 && !strcmp(t + l - 7, " please")) {
        t[l - 7] = 0;
    }
    // "Albert Einstein", then "Black hole", then the words as heard
    for (int form = 0; form < 3; form++) {
        if (form < 2) {
            wt_title(t, s_query, sizeof s_query, form == 0);
        } else {
            snprintf(s_query, sizeof s_query, "%s", t);
        }
        search_run();
        if (s_nres > 0 && !strcmp(s_res_title[0], s_query)) {
            open_entry(s_res[0], true, 0);
            return true;
        }
    }
    wt_title(t, s_query, sizeof s_query, false);  // (no title is the same: the nearest titles)
    search_run();
    s_sc = SC_RESULTS;
    return true;
}

// In a turn of the talk key the speech engine needs about 2 MB of PSRAM: the cluster of the reader goes (one time a
// turn; the article stays in s_doc). The next article from that cluster reads it again.
static bool s_wt_lean;
static void wt_lean(void)
{
    const int ls = listen_state();
    if (ls == LS_REC || ls == LS_HEAR) {
        if (!s_wt_lean && s_z) {
            zim_drop_cache(s_z);
            s_wt_lean = true;
        }
    } else {
        s_wt_lean = false;
    }
}
