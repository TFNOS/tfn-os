// ORBITAL: the missions (included by app_orbital.c). 17 missions; each one teaches one idea. The places of the
// bodies: an orbit (the heights of the periapsis and the apoapsis, the world angle of the periapsis, the angle of
// the body now, the way round) or a place relative to the tug (m along its way and up, m/s).

typedef struct {
    float pe, ap, wp, ang;
    int8_t dir;
} orbit_t;
enum {
    SH_KESTREL,
    SH_SAT,
    SH_CAPSULE,
    SH_MODULE,
    SH_DEBRIS
};
typedef struct {
    uint8_t shape;  // SH_*
    uint8_t rel;    // 1: the place is relative to the tug (dx along its way, dy up: m; dvx, dvy: m/s)
    orbit_t o;
    float dx, dy, dvx, dvy;
    float spin;         // rad/s
    uint8_t dark;       // no lights, only the beacon
    uint8_t hook_side;  // 1: the hook takes the harpoon only from its own side (a wreck that turns)
    uint8_t no_shield;  // a capsule with no heat shield
    const char *name;   // the name on the hull and in the HUD (Latin letters in both languages)
} tdef_t;
enum {
    G_DOCK,
    G_BAND,
    G_LIFT,
    G_LAND,
    G_DELIVER2,
    G_DODGE
};
typedef struct {
    const char *title[2], *goal[2], *idea[2];
    orbit_t tug;
    float fuel;  // kg of fuel at the start
    float sun;   // the world angle of the sun (deg)
    uint8_t ntgt;
    tdef_t t[3];
    uint8_t goal_kind;
    float lo, hi;        // the band (km): G_BAND, G_DELIVER2; G_LIFT: lo = the lowest safe periapsis
    uint8_t towing;      // the mission starts with the tow on body 1
    float zone;          // G_LAND: the world angle of the recovery ship at the start (deg; it turns with the planet)
    float star2, star3;  // the fuel (kg) left for 2 and for 3 stars
    float time3;         // the mission time (s) for 3 stars
} mis_t;

#define NMIS 17
static const mis_t MIS[NMIS] = {
        // 1: tilt, burn, tether
        {{"FIRST CATCH", "תפיסה ראשונה"},
         {"CATCH THE KESTREL", "תפסו את קסטרל"},
         {"TURN TO THE MARKER, HOLD BURN, THEN FIRE THE TETHER", "פנו אל הסימן, החזיקו מנוע, ואז ירו את הכבל"},
         {100, 100, 0, -90, 1},
         300,
         -150,
         1,
         {{SH_KESTREL, 1, {0}, 380, 30, 0, 0, 0.04f, 0, 0, 0, "KESTREL"}},
         G_DOCK,
         0,
         0,
         0,
         0,
         240,
         270,
         1500},
        // 2: match the speed of a body that passes
        {{"MATCH SPEED", "השוו מהירות"},
         {"CATCH THE LYRA PROBE", "תפסו את הגשושית LYRA"},
         {"BURN THE WAY IT GOES TO MATCH ITS SPEED", "בערו לכיוון שבו היא נעה כדי להשוות מהירות"},
         {100, 100, 0, -90, 1},
         300,
         -140,
         1,
         {{SH_SAT, 1, {0}, -900, -20, 7, 0, 0.1f, 0, 0, 0, "LYRA"}},
         G_DOCK,
         0,
         0,
         0,
         0,
         230,
         255,
         1500},
        // 3: a burn forward raises the far side
        {{"ONE BURN UP", "בעירה אחת למעלה"},
         {"MEET HERON ON THE HIGH ORBIT", "פגשו את HERON במסלול הגבוה"},
         {"A BURN FORWARD RAISES THE FAR SIDE OF YOUR ORBIT", "בעירה קדימה מגביהה את הצד הרחוק של המסלול"},
         {100, 100, 0, -90, 1},
         380,
         -60,
         1,
         {{SH_SAT, 0, {130, 130, 0, -55, 1}, 0, 0, 0, 0, 0.08f, 0, 0, 0, "HERON"}},
         G_DOCK,
         0,
         0,
         0,
         0,
         240,
         280,
         4000},
        // 4: a burn backward lowers the far side
        {{"ONE BURN DOWN", "בעירה אחת למטה"},
         {"MEET SWIFT ON THE LOW ORBIT", "פגשו את SWIFT במסלול הנמוך"},
         {"A BURN BACKWARD LOWERS THE FAR SIDE", "בעירה אחורה מנמיכה את הצד הרחוק"},
         {140, 140, 0, -90, 1},
         380,
         200,
         1,
         {{SH_SAT, 0, {104, 104, 0, -130, 1}, 0, 0, 0, 0, 0.08f, 0, 0, 0, "SWIFT"}},
         G_DOCK,
         0,
         0,
         0,
         0,
         240,
         280,
         4000},
        // 5: the green band with the tow (two burns)
        {{"INTO THE BAND", "אל הרצועה"},
         {"TOW KESTREL INTO THE GREEN BAND", "גררו את קסטרל אל הרצועה הירוקה"},
         {"RAISE THE FAR SIDE, THEN THE NEAR SIDE", "הגביהו את הצד הרחוק, ואז את הצד הקרוב"},
         {100, 100, 0, -90, 1},
         520,
         -120,
         1,
         {{SH_KESTREL, 1, {0}, -58, 0, 0, 0, 0.0f, 0, 0, 0, "KESTREL"}},
         G_BAND,
         150,
         170,
         1,
         0,
         240,
         300,
         2800},
        // 6: the mass of a heavy tow
        {{"HEAVY LIFT", "משא כבד"},
         {"TOW THE BARGE INTO THE BAND", "גררו את הדוברה אל הרצועה"},
         {"A HEAVY TOW: THE SAME FUEL GIVES LESS SPEED", "גרירה כבדה: אותו דלק נותן פחות מהירות"},
         {100, 100, 0, -90, 1},
         360,
         -100,
         1,
         {{SH_MODULE, 1, {0}, 420, 20, 0, 0, 0.02f, 0, 0, 0, "BARGE"}},
         G_BAND,
         118,
         132,
         0,
         0,
         140,
         180,
         3000},
        // 7: to catch up, slow down
        {{"CATCH UP", "השיגו אותו"},
         {"CATCH IBIS: IT IS AHEAD OF YOU", "השיגו את IBIS: הוא לפניכם"},
         {"TO CATCH UP, SLOW DOWN: A LOWER ORBIT IS FASTER", "כדי להשיג, האטו: מסלול נמוך מהיר יותר"},
         {110, 110, 0, -90, 1},
         420,
         -30,
         1,
         {{SH_SAT, 0, {110, 110, 0, -84, 1}, 0, 0, 0, 0, 0.1f, 0, 0, 0, "IBIS"}},
         G_DOCK,
         0,
         0,
         0,
         0,
         240,
         300,
         4200},
        // 8: a wreck that turns
        {{"TUMBLER", "מסתובב"},
         {"HOOK THE TUMBLING WRECK", "חברו כבל לשבר המסתובב"},
         {"THE HOOK TURNS WITH IT: FIRE WHEN IT FACES YOU", "הוו מסתובב איתו: ירו כשהוא מולכם"},
         {100, 100, 0, -90, 1},
         450,
         -150,
         1,
         {{SH_KESTREL, 1, {0}, 520, 40, 0, 0, 0.55f, 0, 1, 0, "KESTREL"}},
         G_BAND,
         112,
         128,
         0,
         0,
         240,
         320,
         2800},
        // 9: the shadow of the planet
        {{"INTO THE DARK", "אל החושך"},
         {"FIND WREN IN THE SHADOW", "מצאו את WREN בצל"},
         {"IN THE SHADOW OF THE PLANET ONLY YOUR LIGHT SHOWS IT", "בצל הכוכב רק האור שלכם מראה אותו"},
         {100, 100, 0, -90, 1},
         400,
         70,
         1,
         {{SH_SAT, 0, {122, 122, 0, -60, 1}, 0, 0, 0, 0, 0.06f, 1, 0, 0, "WREN"}},
         G_DOCK,
         0,
         0,
         0,
         0,
         240,
         280,
         4200},
        // 10: a wreck that falls: lift its low side out of the air
        {{"THE FALL", "הנפילה"},
         {"SAVE GANNET BEFORE IT BURNS UP", "הצילו את GANNET לפני שהוא נשרף"},
         {"IT DIPS INTO THE AIR AT ITS LOW POINT: LIFT IT", "הוא טובל באוויר בנקודה הנמוכה: הרימו אותו"},
         {125, 125, 0, -90, 1},
         400,
         -80,
         1,
         {{SH_KESTREL, 1, {0}, 3000, 0, -55, 0, 0.05f, 0, 0, 0, "GANNET"}},
         G_LIFT,
         75,
         0,
         0,
         0,
         220,
         270,
         3200},
        // 11: the air can brake a tow
        {{"AIR BRAKE", "בלימה באוויר"},
         {"BRING KESTREL DOWN INTO THE BAND", "הורידו את קסטרל אל הרצועה"},
         {"THE AIR CAN SLOW YOU: DIP IN, BUT NOT TOO DEEP", "האוויר יכול להאט אתכם: טבלו בו, אבל לא עמוק מדי"},
         {250, 260, 0, -90, 1},
         680,
         -160,
         1,
         {{SH_KESTREL, 1, {0}, -58, 0, 0, 0, 0.0f, 0, 0, 0, "KESTREL"}},
         G_BAND,
         90,
         112,
         1,
         0,
         100,
         130,
         14000},
        // 12: an orbit that goes the other way
        {{"THE OTHER WAY", "בכיוון השני"},
         {"MEET KITE ON ITS HIGH ORBIT", "פגשו את KITE במסלול הגבוה שלו"},
         {"THIS ORBIT GOES THE OTHER WAY: FORWARD IS WHERE YOU FLY",
          "המסלול הזה הולך לכיוון השני: קדימה הוא לאן שאתם טסים"},
         {100, 100, 0, -90, -1},
         380,
         -40,
         1,
         {{SH_SAT, 0, {128, 128, 0, -122, -1}, 0, 0, 0, 0, 0.08f, 0, 0, 0, "KITE"}},
         G_DOCK,
         0,
         0,
         0,
         0,
         240,
         280,
         4000},
        // 13: debris on a head-on orbit
        {{"HEAD-ON", "חזיתי"},
         {"MISS THE DEBRIS, THEN CATCH PIPIT", "התחמקו מהפסולת, ואז תפסו את PIPIT"},
         {"DEBRIS COMES THE OTHER WAY: CHANGE YOUR HEIGHT", "פסולת מגיעה מהכיוון השני: שנו את הגובה"},
         {110, 110, 0, -90, 1},
         450,
         -20,
         2,
         {{SH_SAT, 0, {126, 126, 0, -96, 1}, 0, 0, 0, 0, 0.08f, 0, 0, 0, "PIPIT"},
          {SH_DEBRIS, 0, {110, 110, 0, 80, -1}, 0, 0, 0, 0, 0, 0, 0, 0, "DEBRIS"}},
         G_DODGE,
         0,
         0,
         0,
         0,
         240,
         300,
         3600},
        // 14: two wrecks, one tank
        {{"TWO WRECKS", "שני שברים"},
         {"BRING BOTH WRECKS INTO THE BAND", "הביאו את שני השברים אל הרצועה"},
         {"ONE TANK FOR TWO JOBS: SPEND IT WELL", "מכל אחד לשתי משימות: השתמשו בו בחוכמה"},
         {100, 100, 0, -90, 1},
         900,
         -100,
         2,
         {{SH_KESTREL, 1, {0}, 450, 20, 0, 0, 0.02f, 0, 0, 0, "KESTREL"},
          {SH_SAT, 0, {170, 170, 0, -20, 1}, 0, 0, 0, 0, 0.08f, 0, 0, 0, "PLOVER"}},
         G_DELIVER2,
         128,
         142,
         0,
         0,
         260,
         370,
         10000},
        // 15: a capsule down to the ship
        {{"SPLASHDOWN", "נחיתה בים"},
         {"BRING THE CREW DOWN NEAR THE SHIP", "הורידו את הצוות ליד הספינה"},
         {"LET GO WHEN ITS FALL ENDS AT THE SHIP - THEN SAVE YOURSELF",
          "שחררו כשהנפילה מסתיימת ליד הספינה - ואז הצילו את עצמכם"},
         {110, 110, 0, -90, 1},
         520,
         -90,
         1,
         {{SH_CAPSULE, 1, {0}, 360, 25, 0, 0, 0.05f, 0, 0, 0, "CREW"}},
         G_LAND,
         0,
         0,
         0,
         105,
         100,
         150,
         4000},
        // 16: a capsule that comes down at its next low point
        {{"HOT CATCH", "תפיסה חמה"},
         {"LIFT PETREL BEFORE ITS LOW POINT", "הרימו את PETREL לפני הנקודה הנמוכה"},
         {"NO HEAT SHIELD: IT BURNS AT ITS LOW POINT", "אין מגן חום: הוא נשרף בנקודה הנמוכה"},
         {128, 128, 0, -90, 1},
         520,
         -100,
         1,
         {{SH_CAPSULE, 1, {0}, 9000, 400, -80, 0, 0.05f, 0, 0, 1, "PETREL"}},
         G_LIFT,
         75,
         0,
         0,
         0,
         240,
         280,
         2000},
        // 17: the last mission: dark, turning, down to the ship
        {{"LAST LIGHT", "האור האחרון"},
         {"THE LAST CREW: BRING THEM HOME", "הצוות האחרון: הביאו אותם הביתה"},
         {"IT TURNS, IT IS DARK, AND THE FUEL IS SHORT", "הוא מסתובב, חשוך, והדלק מעט"},
         {110, 110, 0, -90, 1},
         420,
         60,
         1,
         {{SH_CAPSULE, 1, {0}, 600, 45, 0, 0, 0.45f, 1, 1, 0, "ORPHEUS"}},
         G_LAND,
         0,
         0,
         0,
         150,
         120,
         170,
         4500},
};

// ---- the start of a mission: the bodies from the table
static int s_mis;             // the mission now
static float s_spin0;         // the angle of the planet at the start of the mission (rad)
#define PLANET_SPIN 0.00058f  // rad/s: the planet turns once in 3 hours
static void orbit_state(const orbit_t *o, double *x, double *y, float *vx, float *vy)
{
    float rp = R_PL + o->pe, ra = R_PL + o->ap, a = (rp + ra) / 2, e = (ra - rp) / (ra + rp);
    float ang = o->ang * (PI_F / 180), nu = o->dir * (ang - o->wp * (PI_F / 180));
    float p = a * (1 - e * e), r = p / (1 + e * cosf(nu)), k = sqrtf(MU_PL / p);
    float vr = k * e * sinf(nu), vt = k * (1 + e * cosf(nu));
    float cx = cosf(ang), sy = sinf(ang);
    *x = (double)(r * cx);
    *y = (double)(r * sy);
    *vx = vr * cx - o->dir * vt * sy;
    *vy = vr * sy + o->dir * vt * cx;
}
// the local frame of the tug: the unit along its way (prograde horizontal) and up (away from the planet)
static void local_axes(float *ax, float *ay, float *ux, float *uy)
{
    float x = (float)B[0].x, y = (float)B[0].y, r = sqrtf(x * x + y * y);
    *ux = x / r;
    *uy = y / r;
    float hz = x * B[0].vy - y * B[0].vx, s = hz >= 0 ? 1.0f : -1.0f;
    *ax = -*uy * s;
    *ay = *ux * s;
}
static void shape_body(body_t *b, int shape, bool no_shield)
{
    // the mass, the drag area, the heat and the hull of each kind (m)
    switch (shape) {
    case SH_KESTREL:
        b->kind = K_WRECK;
        b->mass = 2000;
        b->cda = 44;
        b->heat_k = 5.0f;
        b->len = 36;
        b->rad = 8;
        b->hx = -42;
        b->hy = 0;
        break;
    case SH_SAT:
        b->kind = K_SAT;
        b->mass = 600;
        b->cda = 12;
        b->heat_k = 4.5f;
        b->len = 5;
        b->rad = 6;
        b->hx = -9;
        b->hy = 0;
        break;
    case SH_CAPSULE:
        b->kind = K_CAPSULE;
        b->mass = 1500;
        b->cda = 6;
        b->heat_k = no_shield ? 5.0f : 0.15f;
        b->len = 3;
        b->rad = 6;
        b->hx = -8;
        b->hy = 0;
        break;
    case SH_MODULE:
        b->kind = K_STATION;
        b->mass = 4000;
        b->cda = 60;
        b->heat_k = 4.0f;
        b->len = 28;
        b->rad = 11;
        b->hx = -38;
        b->hy = 0;
        break;
    default:
        b->kind = K_DEBRIS;
        b->mass = 100;
        b->cda = 1;
        b->heat_k = 6.0f;
        b->len = 0;
        b->rad = 0;
        break;
    }
    b->inertia = b->mass * (4 * b->len * b->len + 3 * b->rad * b->rad) / 12.0f + 1;
}
static void mission_bodies(int m)
{
    const mis_t *M = &MIS[m];
    memset(B, 0, sizeof B);
    memset(&T, 0, sizeof T);
    s_nb = 1 + M->ntgt;
    s_clock = 0;
    s_fuel = M->fuel;
    s_throttle = 0;
    s_nose = 0;
    s_sun = M->sun * (PI_F / 180);
    s_spin0 = 0;
    body_t *t = &B[0];
    t->kind = K_TUG;
    t->mass = TUG_DRY;
    t->cda = 8.8f;
    t->heat_k = 3.3f;
    t->len = 0;
    t->rad = TUG_RAD;
    t->on = 1;
    orbit_state(&M->tug, &t->x, &t->y, &t->vx, &t->vy);
    float ax, ay, ux, uy;
    local_axes(&ax, &ay, &ux, &uy);
    for (int i = 0; i < M->ntgt; i++) {
        const tdef_t *d = &M->t[i];
        body_t *b = &B[1 + i];
        shape_body(b, d->shape, d->no_shield);
        b->on = 1;
        if (d->rel) {
            b->x = t->x + (double)((ax * d->dx + ux * d->dy) * 0.001f);
            b->y = t->y + (double)((ay * d->dx + uy * d->dy) * 0.001f);
            b->vx = t->vx + (ax * d->dvx + ux * d->dvy) * 0.001f;
            b->vy = t->vy + (ay * d->dvx + uy * d->dvy) * 0.001f;
        } else {
            orbit_state(&d->o, &b->x, &b->y, &b->vx, &b->vy);
        }
        // (the hull lies along its way)
        b->ang = atan2f(b->vy, b->vx) + PI_F;
        b->w = d->spin;
        if (d->hook_side) {
            b->hnx = -1;
            b->hny = 0;
        }
    }
    if (M->towing) {
        // (the tow is on at the start: the rope locked at its short length)
        T.state = TH_ON;
        T.to = 1;
        T.L = ROPE_SHORT;
        T.L_goal = ROPE_SHORT;
        B[1].ang = atan2f(B[0].vy, B[0].vx) + PI_F;
        B[1].w = 0;
        // (the hook of the wreck at the rope's end behind the tug)
        double hx, hy;
        hook_world(&B[1], &hx, &hy);
        B[1].x += B[0].x - (double)(ax * ROPE_SHORT * 0.001f) - hx;
        B[1].y += B[0].y - (double)(ay * ROPE_SHORT * 0.001f) - hy;
    }
    forces(0);
}
