// TRIVIA: the question bank and the save (included by app_trivia.c).
// The bank (tv_bank.c; tools/build_bank.py writes it) inflates into PSRAM at the open. The save file keeps the questions
// that each level asked (no question comes again until the level has asked all of its questions), the best scores and
// the round that goes on (CONTINUE).

typedef struct {
    uint8_t lvl, subj, right;
    const char *q, *a[4], *fact;
} quest_t;

static uint8_t *s_bank;  // the inflated bank (TV_BANK_RAW bytes)
static const int LVL_N[3] = {TV_BANK_L0, TV_BANK_L1, TV_BANK_L2};
static const int LVL_FIRST[3] = {0, TV_BANK_L0, TV_BANK_L0 + TV_BANK_L1};  // the bank is sorted by the level

static bool bank_load(void)
{
    if (s_bank) {
        return true;
    }
    s_bank = heap_caps_malloc(TV_BANK_RAW + 4, MALLOC_CAP_SPIRAM);
    if (!s_bank || !inflate_to(TV_BANK_Z, TV_BANK_ZLEN, s_bank, TV_BANK_RAW) ||
        (s_bank[0] | s_bank[1] << 8) != TV_BANK_N) {
        free(s_bank);
        s_bank = NULL;
        return false;
    }
    return true;
}
// question i of the bank (0 .. TV_BANK_N - 1)
static void quest_get(int i, quest_t *q)
{
    memset(q, 0, sizeof *q);
    if (!s_bank || i < 0 || i >= TV_BANK_N) {
        q->q = q->a[0] = q->a[1] = q->a[2] = q->a[3] = q->fact = "";
        return;
    }
    const uint8_t *t = s_bank + 2 + 4 * i;
    const uint8_t *p = s_bank + (t[0] | t[1] << 8 | t[2] << 16 | (uint32_t)t[3] << 24);
    q->lvl = p[0];
    q->subj = p[1] < TV_NSUBJ ? p[1] : 0;
    q->right = p[2] & 3;
    const char *s = (const char *)p + 4;
    const char **dst[6] = {&q->q, &q->a[0], &q->a[1], &q->a[2], &q->a[3], &q->fact};
    for (int k = 0; k < 6; k++) {
        *dst[k] = s;
        s += strlen(s) + 1;
    }
}

// ---------------------------------------------------------------- the save
#define SAVE_FILE  "/data/trivia.sav"
#define SAVE_MAGIC 0x31565254u  // "TRV1"
#define ROUND_Q    10
#define SEEN_BYTES 64  // 512 questions of a level at most
enum {
    MODE_SOLO,
    MODE_PASS,
    MODE_NET
};
typedef struct {
    uint32_t magic, hash;
    uint8_t seen[3][SEEN_BYTES];  // the questions of each level that a round asked (a bit for each)
    uint32_t best[3];             // the best score of a round of one player, of each level
    uint32_t rounds, asked, right;
    // the round that goes on (MODE_SOLO or MODE_PASS; r_on = 0: none)
    uint8_t r_on, r_mode, r_lvl, r_cur;
    uint16_t r_q[ROUND_Q];  // the questions (the index in the bank)
    uint16_t r_score[2];
    uint8_t r_streak[2], r_right[2], r_best_streak[2];
    uint8_t r_res[ROUND_Q];  // the result of each question: 0 = not yet, 1 = right, 2 = wrong, 3 = no answer
} save_t;
static save_t S;

static void save_write(void)
{
    if (storage_ok()) {
        storage_write(SAVE_FILE, &S, sizeof S);
    }
}
static void save_load(void)
{
    size_t n = 0;
    memset(&S, 0, sizeof S);
    if (!storage_ok() || !storage_read(SAVE_FILE, &S, sizeof S, &n) || n != sizeof S || S.magic != SAVE_MAGIC) {
        memset(&S, 0, sizeof S);
    }
    if (S.hash != TV_BANK_HASH) {
        // a new bank: the questions moved; the scores stay, the list of the asked questions and the round go
        memset(S.seen, 0, sizeof S.seen);
        S.r_on = 0;
    }
    S.magic = SAVE_MAGIC;
    S.hash = TV_BANK_HASH;
    if (S.r_lvl > 2 || S.r_cur >= ROUND_Q || S.r_mode > MODE_PASS) {
        S.r_on = 0;
    }
}
static bool seen_get(int lvl, int k)
{
    return (S.seen[lvl][k >> 3] >> (k & 7)) & 1;
}
static void seen_set(int lvl, int k, bool on)
{
    if (on) {
        S.seen[lvl][k >> 3] |= (uint8_t)(1 << (k & 7));
    } else {
        S.seen[lvl][k >> 3] &= (uint8_t)~(1 << (k & 7));
    }
}
// the questions of the level that no round asked yet
static int fresh_count(int lvl)
{
    int n = 0;
    for (int k = 0; k < LVL_N[lvl]; k++) {
        n += !seen_get(lvl, k);
    }
    return n;
}
// mark question i (the bank index) as asked
static void seen_mark(int i)
{
    for (int l = 0; l < 3; l++) {
        if (i >= LVL_FIRST[l] && i < LVL_FIRST[l] + LVL_N[l]) {
            seen_set(l, i - LVL_FIRST[l], true);
        }
    }
}
// pick n questions of the level for a round (bank indices into out): only questions that no round asked; when the level
// has fewer than n left, those come first, then the list of the level starts again (with no question of this round)
static void round_pick(int lvl, uint16_t *out, int n)
{
    int got = 0;
    for (int pass = 0; pass < 2 && got < n; pass++) {
        int left = fresh_count(lvl);
        if (left == 0) {
            memset(S.seen[lvl], 0, SEEN_BYTES);
            for (int j = 0; j < got; j++) {
                seen_set(lvl, out[j] - LVL_FIRST[lvl], true);
            }
            left = fresh_count(lvl);
        }
        while (got < n && left > 0) {
            int r = (int)(esp_random() % (uint32_t)left);
            for (int k = 0; k < LVL_N[lvl]; k++) {
                if (seen_get(lvl, k)) {
                    continue;
                }
                if (r-- == 0) {
                    out[got++] = (uint16_t)(LVL_FIRST[lvl] + k);
                    seen_set(lvl, k, true);
                    left--;
                    break;
                }
            }
        }
    }
}
