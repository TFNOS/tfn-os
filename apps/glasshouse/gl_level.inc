// GLASSHOUSE: the level format and its parser. A level is a small text file (apps/glasshouse/levels/NN.txt; the
// app has them built in, gl_levels.inc, and a file /sdcard/glass/lv/NN.txt on the card adds or replaces a level).
// One statement on each line; '#' starts a comment. The places are WORLD px of the diorama (640 x 172 at zoom 1, x
// to the right, y down; the top of the stand is y = 139), the angles are degrees (+ = clockwise on the screen).
//
//   name <english> | <hebrew>          the name of the miniature (the brass label)
//   idea <english> | <hebrew>          the one idea that the level teaches (the card at the start)
//   sky dusk|night|dawn|day|storm|winter|gold        the painted backdrop
//   ground moss|cobble|sand|snow|lawn|planks         the top of the stand
//   object <kind> <x> <y>              the fragile object: it stands with its base at (x, y)
//   moves <n> / par <n>                the moves, and the fewest moves (the second star)
//   goal down|break <tag> <value>      down: <value> percent of the parts with the tag fall or break;
//                                      break: <value> panes (glass) of the parts with the tag break
//   view <x> <y> <zoom>                the aim view
//   <part> <id> <x> <y> <w> <h> [key=value ...]     a part: its middle, its size (a ball: <x> <y> <r>)
//       parts: cell roof post beam plank crate tank ball weight pot sack brick stone pane bottle lens wheel bucket
//              bell lamp girder slab
//       keys: a=<deg> tag=<t1,t2> z=<n> static awake grid=<nx>x<ny> tough=<m/s> dens=<kg/m2> fric=<0..1>
//             damp=<1/s> spill=<drops> look=<n> fixrot nocount (look: 0 plain; a cell 1 = plants inside, 2 = frosted, 3 = stained)
//   poly <id> <part> <x1> <y1> <x2> <y2> ... [keys]  a part with a polygon (3 to 8 points)
//   weld <id> <a> <b> <x> <y> [break=<N>] [torque=<Nm>]          (a, b: part ids or "ground")
//   hinge <id> <a> <b> <x> <y> [fric=<Nm>] [lo=<deg>] [hi=<deg>] [break=<N>]
//   rope <id> <a> <x1> <y1> <b> <x2> <y2> [len=<px>] [look=rope|chain|cable] [break=<N>] [sub=<k>]
//       (sub: the rope is tied to the glass pane k of the cell a: it goes when that pane breaks)
//   rod <id> <a> <x1> <y1> <b> <x2> <y2>               a rigid bar of no mass between two points
//   motor <id> <a> <b> <x> <y> speed=<deg/s> torque=<Nm>    a hinge with a motor (off until START)
//   pipe <id> <x> <y> <dx> <dy> rate=<drops/s> time=<s>      a valve: water comes out when it opens
//   winch <id> rope=<rope id> speed=<px/s> min=<px>          START winds the rope in
//   move cut|pull|crack|kick|open|start <target> [at=<x>,<y>] [dir=<dx>,<dy>] [power=<m/s>] [sub=<k>]
//   deco <kind> <x> <y> [args]         scenery (no physics)
//   solve <t> <move> / fail <t> <move> the moves of a solution (t: seconds after the first move; <move>: the
//                                      target id of a move) and of a wrong way that breaks the object (the check)

#define MAXP 72   // parts
#define MAXJ 120  // joints
#define MAXH 6    // moves (handles)
#define MAXD 40   // deco items
#define MAXE 4    // pipes and winches
#define MAXS 8    // the steps of a solution
#define NSUB 6    // the glass panes of a cell, at most

enum {
    PK_CELL,
    PK_ROOF,
    PK_POST,
    PK_BEAM,
    PK_PLANK,
    PK_CRATE,
    PK_TANK,
    PK_BALL,
    PK_WEIGHT,
    PK_POT,
    PK_SACK,
    PK_BRICK,
    PK_STONE,
    PK_PANE,
    PK_BOTTLE,
    PK_LENS,
    PK_WHEEL,
    PK_BUCKET,
    PK_BELL,
    PK_LAMP,
    PK_GIRDER,
    PK_SLAB,
    PK_OBJ,
    PK_STAND,
    PK_N
};
static const char *const PK_NAME[PK_N] = {"cell",   "roof",   "post", "beam",  "plank",  "crate", "tank",   "ball",
                                          "weight", "pot",    "sack", "brick", "stone",  "pane",  "bottle", "lens",
                                          "wheel",  "bucket", "bell", "lamp",  "girder", "slab",  "object", "stand"};
enum {
    SH_BOX,
    SH_POLY,
    SH_CIRCLE
};
enum {
    J_WELD,
    J_HINGE,
    J_ROPE,
    J_ROD,
    J_MOTOR
};
enum {
    MV_CUT,
    MV_PULL,
    MV_CRACK,
    MV_KICK,
    MV_OPEN,
    MV_START,
    MV_N
};
static const char *const MV_NAME[MV_N] = {"cut", "pull", "crack", "kick", "open", "start"};
enum {
    SKY_DUSK,
    SKY_NIGHT,
    SKY_DAWN,
    SKY_DAY,
    SKY_STORM,
    SKY_WINTER,
    SKY_GOLD,
    SKY_N
};
static const char *const SKY_NAME[SKY_N] = {"dusk", "night", "dawn", "day", "storm", "winter", "gold"};
enum {
    GR_MOSS,
    GR_COBBLE,
    GR_SAND,
    GR_SNOW,
    GR_LAWN,
    GR_PLANKS,
    GR_N
};
static const char *const GR_NAME[GR_N] = {"moss", "cobble", "sand", "snow", "lawn", "planks"};
enum {
    GOAL_DOWN,
    GOAL_BREAK
};
#define PF_STATIC  1
#define PF_AWAKE   2
#define PF_FIXROT  4
#define PF_NOCOUNT 8

typedef struct {
    char id[12];
    uint8_t kind, shape, flags, z, nx, ny, look, npt;
    uint16_t tags;
    float x, y, w, h, a;      // the middle, the size (px), the angle (rad, + = clockwise on the screen)
    float pt[16];             // SH_POLY: the points (world px)
    float tough, dens, fric;  // < 0: the value of the kind
    float damp;               // the damping of its motion (1/s; 0 = the default)
    int16_t spill;
} pdef_t;
typedef struct {
    char id[12];
    uint8_t kind, look;
    int8_t a, b;           // the parts (-1 = the ground)
    int8_t sub;            // a rope tied to a glass pane of the cell a (-1 = none)
    float x1, y1, x2, y2;  // the anchors (world px)
    float len, brk, brkt, fric, speed, torque, lo, hi;
    bool limit;
} jdef_t;
enum {
    TK_JOINT,
    TK_PART,
    TK_EMIT
};
typedef struct {
    uint8_t kind;   // MV_*
    uint8_t tkind;  // TK_*: the target is a joint (cut, pull, start a motor), a part (crack, kick)
                    // or a pipe / a winch (open, start)
    int16_t target;
    int8_t sub;           // crack: the pane (-1 = the nearest to the point)
    float hx, hy;         // the point of the handle (world px; 0, 0 = from the target)
    float dx, dy, power;  // kick: the way and the change of speed (m/s)
} hdef_t;
typedef struct {
    char id[12];
    uint8_t kind;  // 0 a pipe, 1 a winch
    int8_t rope;   // a winch: its rope (a joint)
    float x, y, dx, dy, rate, time, speed, min;
} edef_t;
typedef struct {
    char kind[12];
    float x, y, p[4];
} ddef_t;
typedef struct {
    float t;
    int8_t h;
} sstep_t;

typedef struct {
    char name[2][72], idea[2][144];  // (UTF-8: a Hebrew letter has 2 bytes)
    char tagname[8][10];
    int ntag;
    uint8_t sky, ground, obj, moves, par, goal, goal_tag;
    float obj_x, obj_y, goal_v, view_x, view_y, view_z;
    pdef_t p[MAXP];
    int np;
    jdef_t j[MAXJ];
    int nj;
    hdef_t h[MAXH];
    int nh;
    edef_t e[MAXE];
    int ne;
    ddef_t d[MAXD];
    int nd;
    sstep_t sol[MAXS], bad[MAXS];
    int nsol, nbad;
    int errors;
} level_t;

// ---------------------------------------------------------------- the parser
static char s_lv_err[96];
static void lv_err(level_t *L, int line, const char *what)
{
    if (!L->errors) {
        snprintf(s_lv_err, sizeof s_lv_err, "line %d: %s", line, what);
    }
    L->errors++;
#ifndef ESP_PLATFORM
    printf("  level line %d: %s\n", line, what);
#endif
}
static int lv_tok(char *s, char **tok, int max)
{
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (!*s || *s == '#') {
            break;
        }
        tok[n++] = s;
        while (*s && *s != ' ' && *s != '\t') {
            s++;
        }
        if (*s) {
            *s++ = 0;
        }
    }
    return n;
}
static int lv_part(const level_t *L, const char *id)
{
    if (!strcmp(id, "ground")) {
        return -1;
    }
    for (int i = 0; i < L->np; i++) {
        if (!strcmp(L->p[i].id, id)) {
            return i;
        }
    }
    return -2;
}
static int lv_joint(const level_t *L, const char *id)
{
    for (int i = 0; i < L->nj; i++) {
        if (!strcmp(L->j[i].id, id)) {
            return i;
        }
    }
    return -1;
}
static int lv_emit(const level_t *L, const char *id)
{
    for (int i = 0; i < L->ne; i++) {
        if (!strcmp(L->e[i].id, id)) {
            return i;
        }
    }
    return -1;
}
static int lv_tag(level_t *L, const char *t)
{
    for (int i = 0; i < L->ntag; i++) {
        if (!strcmp(L->tagname[i], t)) {
            return i;
        }
    }
    if (L->ntag >= 8) {
        return -1;
    }
    snprintf(L->tagname[L->ntag], sizeof L->tagname[0], "%s", t);
    return L->ntag++;
}
// "key=value": the value, or NULL
static const char *lv_key(char **tok, int n, int from, const char *key)
{
    size_t k = strlen(key);
    for (int i = from; i < n; i++) {
        if (!strncmp(tok[i], key, k) && tok[i][k] == '=') {
            return tok[i] + k + 1;
        }
    }
    return NULL;
}
static bool lv_flag(char **tok, int n, int from, const char *key)
{
    for (int i = from; i < n; i++) {
        if (!strcmp(tok[i], key)) {
            return true;
        }
    }
    return false;
}
static float lv_f(char **tok, int n, int from, const char *key, float def)
{
    const char *v = lv_key(tok, n, from, key);
    return v ? strtof(v, NULL) : def;
}
// the text of "name EN | HE": the two halves
static void lv_text2(const char *s, char *en, char *he, int max)
{
    while (*s == ' ') {
        s++;
    }
    const char *bar = strchr(s, '|');
    int le = bar ? (int)(bar - s) : (int)strlen(s);
    while (le > 0 && (s[le - 1] == ' ' || s[le - 1] == '\r')) {
        le--;
    }
    snprintf(en, (size_t)max, "%.*s", le, s);
    if (bar) {
        const char *h = bar + 1;
        while (*h == ' ') {
            h++;
        }
        int lh = (int)strlen(h);
        while (lh > 0 && (h[lh - 1] == ' ' || h[lh - 1] == '\r' || h[lh - 1] == '\n')) {
            lh--;
        }
        snprintf(he, (size_t)max, "%.*s", lh, h);
    } else {
        snprintf(he, (size_t)max, "%s", en);
    }
}
static int lv_find(const char *const *names, int n, const char *s)
{
    for (int i = 0; i < n; i++) {
        if (!strcmp(names[i], s)) {
            return i;
        }
    }
    return -1;
}
static int obj_kind_of(const char *s);
// the move whose target has this id (a joint, a part or a pipe)
static int lv_move_of(const level_t *L, const char *id)
{
    for (int i = 0; i < L->nh; i++) {
        const hdef_t *h = &L->h[i];
        const char *t = h->tkind == TK_JOINT  ? L->j[h->target].id
                        : h->tkind == TK_PART ? L->p[h->target].id
                                              : L->e[h->target].id;
        if (!strcmp(t, id)) {
            return i;
        }
    }
    return -1;
}

// parse the text of a level into L; true when it has no error
static bool lv_parse(level_t *L, const char *text)
{
    memset(L, 0, sizeof *L);
    L->moves = 3;
    L->par = 1;
    L->view_x = 320;
    L->view_y = 86;
    L->view_z = 1.12f;
    L->obj_x = 320;
    L->obj_y = 139;
    L->goal = GOAL_DOWN;
    L->goal_v = 50;
    char line[320], *tok[40];
    int ln = 0;
    const char *s = text;
    while (*s) {
        int k = 0;
        while (*s && *s != '\n' && k < (int)sizeof line - 1) {
            line[k++] = *s++;
        }
        while (*s && *s != '\n') {
            s++;
        }
        if (*s == '\n') {
            s++;
        }
        line[k] = 0;
        ln++;
        while (k > 0 && (line[k - 1] == '\r' || line[k - 1] == ' ')) {
            line[--k] = 0;
        }
        // (the texts first: they have spaces)
        char *l0 = line;
        while (*l0 == ' ') {
            l0++;
        }
        if (!strncmp(l0, "name ", 5)) {
            lv_text2(l0 + 5, L->name[0], L->name[1], (int)sizeof L->name[0]);
            continue;
        }
        if (!strncmp(l0, "idea ", 5)) {
            lv_text2(l0 + 5, L->idea[0], L->idea[1], (int)sizeof L->idea[0]);
            continue;
        }
        int n = lv_tok(line, tok, 40);
        if (n == 0) {
            continue;
        }
        const char *c = tok[0];
        if (!strcmp(c, "glasshouse")) {
            continue;
        }
        if (!strcmp(c, "sky") && n > 1) {
            int v = lv_find(SKY_NAME, SKY_N, tok[1]);
            if (v < 0) {
                lv_err(L, ln, "sky");
            } else {
                L->sky = (uint8_t)v;
            }
            continue;
        }
        if (!strcmp(c, "ground") && n > 1) {
            int v = lv_find(GR_NAME, GR_N, tok[1]);
            if (v < 0) {
                lv_err(L, ln, "ground");
            } else {
                L->ground = (uint8_t)v;
            }
            continue;
        }
        if (!strcmp(c, "object") && n > 3) {
            int v = obj_kind_of(tok[1]);
            if (v < 0) {
                lv_err(L, ln, "object kind");
            } else {
                L->obj = (uint8_t)v;
            }
            L->obj_x = strtof(tok[2], NULL);
            L->obj_y = strtof(tok[3], NULL);
            continue;
        }
        if (!strcmp(c, "moves") && n > 1) {
            L->moves = (uint8_t)clampi(atoi(tok[1]), 1, 5);
            continue;
        }
        if (!strcmp(c, "par") && n > 1) {
            L->par = (uint8_t)clampi(atoi(tok[1]), 1, 5);
            continue;
        }
        if (!strcmp(c, "goal") && n > 3) {
            L->goal = !strcmp(tok[1], "break") ? GOAL_BREAK : GOAL_DOWN;
            int t = lv_tag(L, tok[2]);
            L->goal_tag = (uint8_t)(t < 0 ? 0 : t);
            L->goal_v = strtof(tok[3], NULL);
            continue;
        }
        if (!strcmp(c, "view") && n > 3) {
            L->view_x = strtof(tok[1], NULL);
            L->view_y = strtof(tok[2], NULL);
            L->view_z = strtof(tok[3], NULL);
            continue;
        }
        // ---- a part
        int pk = lv_find(PK_NAME, PK_N, c);
        bool poly = !strcmp(c, "poly");
        if (pk >= 0 || poly) {
            if (L->np >= MAXP) {
                lv_err(L, ln, "too many parts");
                continue;
            }
            pdef_t *P = &L->p[L->np];
            memset(P, 0, sizeof *P);
            int from;
            if (poly) {
                if (n < 9) {
                    lv_err(L, ln, "poly");
                    continue;
                }
                pk = lv_find(PK_NAME, PK_N, tok[2]);
                if (pk < 0) {
                    lv_err(L, ln, "poly kind");
                    continue;
                }
                snprintf(P->id, sizeof P->id, "%s", tok[1]);
                int i = 3, m = 0;
                while (i + 1 < n && m < 8 && !strchr(tok[i], '=') && !strchr(tok[i + 1], '=') &&
                       (tok[i][0] == '-' || (tok[i][0] >= '0' && tok[i][0] <= '9'))) {
                    P->pt[2 * m] = strtof(tok[i], NULL);
                    P->pt[2 * m + 1] = strtof(tok[i + 1], NULL);
                    m++;
                    i += 2;
                }
                if (m < 3) {
                    lv_err(L, ln, "poly points");
                    continue;
                }
                P->npt = (uint8_t)m;
                P->shape = SH_POLY;
                float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
                for (int q = 0; q < m; q++) {
                    x0 = fminf(x0, P->pt[2 * q]);
                    x1 = fmaxf(x1, P->pt[2 * q]);
                    y0 = fminf(y0, P->pt[2 * q + 1]);
                    y1 = fmaxf(y1, P->pt[2 * q + 1]);
                }
                P->x = (x0 + x1) / 2;
                P->y = (y0 + y1) / 2;
                P->w = x1 - x0;
                P->h = y1 - y0;
                from = i;
            } else {
                if (n < (pk == PK_BALL || pk == PK_WHEEL || pk == PK_LENS ? 5 : 6)) {
                    lv_err(L, ln, "part");
                    continue;
                }
                snprintf(P->id, sizeof P->id, "%s", tok[1]);
                P->x = strtof(tok[2], NULL);
                P->y = strtof(tok[3], NULL);
                if (pk == PK_BALL || pk == PK_WHEEL || pk == PK_LENS) {
                    P->w = P->h = 2 * strtof(tok[4], NULL);
                    P->shape = SH_CIRCLE;
                    from = 5;
                } else {
                    P->w = strtof(tok[4], NULL);
                    P->h = strtof(tok[5], NULL);
                    P->shape = SH_BOX;
                    from = 6;
                }
            }
            P->kind = (uint8_t)pk;
            P->a = lv_f(tok, n, from, "a", 0) * PI_ / 180.0f;
            P->tough = lv_f(tok, n, from, "tough", -1);
            P->dens = lv_f(tok, n, from, "dens", -1);
            P->fric = lv_f(tok, n, from, "fric", -1);
            P->damp = lv_f(tok, n, from, "damp", 0);
            P->spill = (int16_t)lv_f(tok, n, from, "spill", 0);
            P->z = (uint8_t)lv_f(tok, n, from, "z", pk == PK_CELL || pk == PK_ROOF ? 3 : 5);
            if (lv_flag(tok, n, from, "static")) {
                P->flags |= PF_STATIC;
            }
            if (lv_flag(tok, n, from, "awake")) {
                P->flags |= PF_AWAKE;
            }
            if (lv_flag(tok, n, from, "fixrot")) {
                P->flags |= PF_FIXROT;
            }
            if (lv_flag(tok, n, from, "nocount")) {
                P->flags |= PF_NOCOUNT;
            }
            const char *g = lv_key(tok, n, from, "grid");
            P->nx = P->ny = 1;
            if (g) {
                P->nx = (uint8_t)clampi(atoi(g), 1, 3);
                const char *x = strchr(g, 'x');
                if (x) {
                    P->ny = (uint8_t)clampi(atoi(x + 1), 1, 3);
                }
            }
            while (P->nx * P->ny > NSUB) {
                P->ny--;
            }
            const char *lk = lv_key(tok, n, from, "look");
            P->look = lk ? (uint8_t)clampi(atoi(lk), 0, 9) : 0;
            const char *tg = lv_key(tok, n, from, "tag");
            if (tg) {
                char buf[48];
                snprintf(buf, sizeof buf, "%s", tg);
                for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
                    int ti = lv_tag(L, t);
                    if (ti >= 0) {
                        P->tags |= (uint16_t)(1u << ti);
                    }
                }
            }
            if (lv_part(L, P->id) != -2) {
                lv_err(L, ln, "the part id is used");
                continue;
            }
            L->np++;
            continue;
        }
        // ---- a joint
        int jk = !strcmp(c, "weld")    ? J_WELD
                 : !strcmp(c, "hinge") ? J_HINGE
                 : !strcmp(c, "rope")  ? J_ROPE
                 : !strcmp(c, "rod")   ? J_ROD
                 : !strcmp(c, "motor") ? J_MOTOR
                                       : -1;
        if (jk >= 0) {
            if (L->nj >= MAXJ) {
                lv_err(L, ln, "too many joints");
                continue;
            }
            jdef_t *J = &L->j[L->nj];
            memset(J, 0, sizeof *J);
            J->kind = (uint8_t)jk;
            J->sub = -1;
            int from;
            if (jk == J_ROPE || jk == J_ROD) {
                if (n < 8) {
                    lv_err(L, ln, "rope");
                    continue;
                }
                snprintf(J->id, sizeof J->id, "%s", tok[1]);
                int a = lv_part(L, tok[2]), b = lv_part(L, tok[5]);
                if (a == -2 || b == -2) {
                    lv_err(L, ln, "rope part");
                    continue;
                }
                J->a = (int8_t)a;
                J->b = (int8_t)b;
                J->x1 = strtof(tok[3], NULL);
                J->y1 = strtof(tok[4], NULL);
                J->x2 = strtof(tok[6], NULL);
                J->y2 = strtof(tok[7], NULL);
                from = 8;
                const char *lk = lv_key(tok, n, from, "look");
                J->look = lk ? (uint8_t)(!strcmp(lk, "chain") ? 1 : !strcmp(lk, "cable") ? 2 : 0) : 0;
                J->len = lv_f(tok, n, from, "len", 0);
                J->sub = (int8_t)lv_f(tok, n, from, "sub", -1);
            } else {
                if (n < 6) {
                    lv_err(L, ln, "joint");
                    continue;
                }
                snprintf(J->id, sizeof J->id, "%s", tok[1]);
                int a = lv_part(L, tok[2]), b = lv_part(L, tok[3]);
                if (a == -2 || b == -2) {
                    lv_err(L, ln, "joint part");
                    continue;
                }
                J->a = (int8_t)a;
                J->b = (int8_t)b;
                J->x1 = J->x2 = strtof(tok[4], NULL);
                J->y1 = J->y2 = strtof(tok[5], NULL);
                from = 6;
            }
            J->brk = lv_f(tok, n, from, "break", 0);
            J->brkt = lv_f(tok, n, from, "torque", 0);
            J->fric = lv_f(tok, n, from, "fric", 0);
            J->speed = lv_f(tok, n, from, "speed", 0) * PI_ / 180.0f;
            if (jk == J_MOTOR) {
                J->torque = lv_f(tok, n, from, "torque", 20), J->brkt = 0;
            }
            if (lv_key(tok, n, from, "lo")) {
                J->limit = true;
                J->lo = lv_f(tok, n, from, "lo", 0) * PI_ / 180.0f;
                J->hi = lv_f(tok, n, from, "hi", 0) * PI_ / 180.0f;
            }
            if (lv_joint(L, J->id) >= 0) {
                lv_err(L, ln, "the joint id is used");
                continue;
            }
            L->nj++;
            continue;
        }
        if ((!strcmp(c, "pipe") || !strcmp(c, "winch")) && n > 1) {
            if (L->ne >= MAXE) {
                lv_err(L, ln, "too many pipes");
                continue;
            }
            edef_t *E = &L->e[L->ne];
            memset(E, 0, sizeof *E);
            snprintf(E->id, sizeof E->id, "%s", tok[1]);
            if (!strcmp(c, "pipe")) {
                if (n < 6) {
                    lv_err(L, ln, "pipe");
                    continue;
                }
                E->kind = 0;
                E->x = strtof(tok[2], NULL);
                E->y = strtof(tok[3], NULL);
                E->dx = strtof(tok[4], NULL);
                E->dy = strtof(tok[5], NULL);
                E->rate = lv_f(tok, n, 6, "rate", 40);
                E->time = lv_f(tok, n, 6, "time", 6);
            } else {
                E->kind = 1;
                const char *r = lv_key(tok, n, 2, "rope");
                E->rope = (int8_t)(r ? lv_joint(L, r) : -1);
                if (E->rope < 0) {
                    lv_err(L, ln, "winch rope");
                    continue;
                }
                E->speed = lv_f(tok, n, 2, "speed", 30);
                E->min = lv_f(tok, n, 2, "min", 10);
            }
            L->ne++;
            continue;
        }
        if (!strcmp(c, "move") && n > 2) {
            if (L->nh >= MAXH) {
                lv_err(L, ln, "too many moves");
                continue;
            }
            hdef_t *H = &L->h[L->nh];
            memset(H, 0, sizeof *H);
            int mk = lv_find(MV_NAME, MV_N, tok[1]);
            if (mk < 0) {
                lv_err(L, ln, "move kind");
                continue;
            }
            H->kind = (uint8_t)mk;
            H->sub = (int8_t)lv_f(tok, n, 3, "sub", -1);
            if (mk == MV_CUT || mk == MV_PULL) {
                H->tkind = TK_JOINT;
                H->target = (int16_t)lv_joint(L, tok[2]);
            } else if (mk == MV_CRACK || mk == MV_KICK) {
                int p = lv_part(L, tok[2]);
                H->tkind = TK_PART;
                H->target = (int16_t)(p < 0 ? -1 : p);
            } else if (mk == MV_OPEN) {
                H->tkind = TK_EMIT;
                H->target = (int16_t)lv_emit(L, tok[2]);
            } else {
                // start: a motor (a joint) or a winch (an emitter entry)
                int j = lv_joint(L, tok[2]), e = lv_emit(L, tok[2]);
                H->tkind = j >= 0 ? TK_JOINT : TK_EMIT;
                H->target = (int16_t)(j >= 0 ? j : e);
            }
            if (H->target < 0) {
                lv_err(L, ln, "move target");
                continue;
            }
            const char *at = lv_key(tok, n, 3, "at");
            if (at) {
                H->hx = strtof(at, NULL);
                const char *cm = strchr(at, ',');
                if (cm) {
                    H->hy = strtof(cm + 1, NULL);
                }
            }
            const char *dir = lv_key(tok, n, 3, "dir");
            H->dx = 1;
            H->dy = 0;
            if (dir) {
                H->dx = strtof(dir, NULL);
                const char *cm = strchr(dir, ',');
                if (cm) {
                    H->dy = strtof(cm + 1, NULL);
                }
            }
            H->power = lv_f(tok, n, 3, "power", 3);
            L->nh++;
            continue;
        }
        if (!strcmp(c, "deco") && n > 3) {
            if (L->nd >= MAXD) {
                lv_err(L, ln, "too many deco");
                continue;
            }
            ddef_t *D = &L->d[L->nd++];
            memset(D, 0, sizeof *D);
            snprintf(D->kind, sizeof D->kind, "%s", tok[1]);
            D->x = strtof(tok[2], NULL);
            D->y = strtof(tok[3], NULL);
            for (int i = 0; i < 4 && 4 + i < n; i++) {
                D->p[i] = strtof(tok[4 + i], NULL);
            }
            continue;
        }
        if ((!strcmp(c, "solve") || !strcmp(c, "fail")) && n > 2) {
            bool good = c[0] == 's';
            int *cnt = good ? &L->nsol : &L->nbad;
            sstep_t *st = good ? L->sol : L->bad;
            if (*cnt >= MAXS) {
                lv_err(L, ln, "too many steps");
                continue;
            }
            int m = lv_move_of(L, tok[2]);
            if (m < 0) {
                lv_err(L, ln, "no move with that target");
                continue;
            }
            st[*cnt].t = strtof(tok[1], NULL);
            st[*cnt].h = (int8_t)m;
            (*cnt)++;
            continue;
        }
        lv_err(L, ln, "what is this");
    }
    if (L->np == 0) {
        lv_err(L, ln, "no parts");
    }
    if (L->nh == 0) {
        lv_err(L, ln, "no moves");
    }
    return L->errors == 0;
}
