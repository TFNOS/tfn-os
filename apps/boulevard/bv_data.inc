// BOULEVARD: the data of the board (40 tiles of Tel Aviv), the rent tables and the two card decks (English and Hebrew).
// No real name of the classic game and none of its streets: the streets and places are real Tel Aviv ones.
// A tile name for the board can have two lines: "A|B".

#define NT     40  // the tiles of the board
#define NPROP  28  // the properties (22 streets, 4 transport tiles, 2 services)
#define MAXP   6   // the players
#define BANK   6   // the bank as a side of a payment
#define NOBODY 0xFF

enum {
    TK_START,
    TK_STREET,
    TK_TRANS,
    TK_UTIL,
    TK_TAX,
    TK_CITY,
    TK_SURP,
    TK_JAM,
    TK_BEACH,
    TK_WORKS
};
enum {
    G_BROWN,
    G_LBLUE,
    G_PINK,
    G_ORANGE,
    G_RED,
    G_YELLOW,
    G_GREEN,
    G_DBLUE,
    G_TRANS,
    G_UTIL,
    G_NONE
};
#define NGROUP      10
#define JAM_TILE    10
#define START_BONUS 200
#define BAIL        50
#define START_CASH  1500

typedef struct {
    uint8_t kind, group;
    uint16_t price;        // a property: its price; a tax: the sum to pay
    uint16_t build;        // a street: the price of one floor
    uint16_t rent[6];      // a street: no floor, 1-4 floors, the tower
    const char *name[2];   // the full name (English, Hebrew)
    const char *board[2];  // the text on the board tile ("A|B" = two lines)
} tile_t;

static const tile_t TILES[NT] = {
        {TK_START, G_NONE, 0, 0, {0}, {"START", "התחלה"}, {"START", "התחלה"}},
        {TK_STREET, G_BROWN, 60, 50, {3, 12, 35, 95, 170, 260}, {"FLORENTIN", "פלורנטין"}, {"FLORENTIN", "פלורנטין"}},
        {TK_CITY, G_NONE, 0, 0, {0}, {"CITY HALL", "העירייה"}, {"CITY HALL", "העירייה"}},
        {TK_STREET,
         G_BROWN,
         60,
         50,
         {4, 20, 60, 180, 310, 440},
         {"KEREM HATEIMANIM", "כרם התימנים"},
         {"KEREM|HATEIMANIM", "כרם|התימנים"}},
        {TK_TAX, G_NONE, 200, 0, {0}, {"ARNONA", "ארנונה"}, {"ARNONA", "ארנונה"}},
        {TK_TRANS, G_TRANS, 200, 0, {0}, {"JAFFA PORT", "נמל יפו"}, {"JAFFA|PORT", "נמל|יפו"}},
        {TK_STREET,
         G_LBLUE,
         100,
         50,
         {6, 30, 90, 270, 400, 540},
         {"CARMEL MARKET", "שוק הכרמל"},
         {"CARMEL|MARKET", "שוק|הכרמל"}},
        {TK_SURP, G_NONE, 0, 0, {0}, {"SURPRISE", "הפתעה"}, {"SURPRISE", "הפתעה"}},
        {TK_STREET,
         G_LBLUE,
         100,
         50,
         {6, 30, 90, 270, 400, 540},
         {"NAHALAT BINYAMIN", "נחלת בנימין"},
         {"NAHALAT|BINYAMIN", "נחלת|בנימין"}},
        {TK_STREET, G_LBLUE, 120, 50, {8, 40, 110, 310, 460, 600}, {"ALLENBY", "אלנבי"}, {"ALLENBY", "אלנבי"}},
        {TK_JAM, G_NONE, 0, 0, {0}, {"AYALON JAM", "פקק באיילון"}, {"AYALON|JAM", "פקק|באיילון"}},
        {TK_STREET, G_PINK, 140, 100, {10, 50, 150, 440, 620, 760}, {"SHEINKIN", "שינקין"}, {"SHEINKIN", "שינקין"}},
        {TK_UTIL, G_UTIL, 150, 0, {0}, {"READING POWER", "תחנת רידינג"}, {"READING|POWER", "תחנת|רידינג"}},
        {TK_STREET,
         G_PINK,
         140,
         100,
         {10, 50, 150, 440, 620, 760},
         {"KING GEORGE", "קינג ג'ורג'"},
         {"KING|GEORGE", "קינג|ג'ורג'"}},
        {TK_STREET, G_PINK, 160, 100, {12, 60, 180, 500, 700, 880}, {"BOGRASHOV", "בוגרשוב"}, {"BOGRASHOV", "בוגרשוב"}},
        {TK_TRANS, G_TRANS, 200, 0, {0}, {"CENTRAL STATION", "התחנה המרכזית"}, {"CENTRAL|STATION", "התחנה|המרכזית"}},
        {TK_STREET,
         G_ORANGE,
         180,
         100,
         {14, 70, 200, 560, 760, 940},
         {"BEN YEHUDA", "בן יהודה"},
         {"BEN|YEHUDA", "בן|יהודה"}},
        {TK_CITY, G_NONE, 0, 0, {0}, {"CITY HALL", "העירייה"}, {"CITY HALL", "העירייה"}},
        {TK_STREET, G_ORANGE, 180, 100, {14, 70, 200, 560, 760, 940}, {"FRISHMAN", "פרישמן"}, {"FRISHMAN", "פרישמן"}},
        {TK_STREET, G_ORANGE, 200, 100, {16, 80, 220, 600, 800, 1000}, {"HAYARKON", "הירקון"}, {"HAYARKON", "הירקון"}},
        {TK_BEACH, G_NONE, 0, 0, {0}, {"GORDON BEACH", "חוף גורדון"}, {"GORDON|BEACH", "חוף|גורדון"}},
        {TK_STREET, G_RED, 220, 150, {18, 90, 250, 700, 880, 1060}, {"DIZENGOFF", "דיזנגוף"}, {"DIZENGOFF", "דיזנגוף"}},
        {TK_SURP, G_NONE, 0, 0, {0}, {"SURPRISE", "הפתעה"}, {"SURPRISE", "הפתעה"}},
        {TK_STREET,
         G_RED,
         220,
         150,
         {18, 90, 250, 700, 880, 1060},
         {"IBN GABIROL", "אבן גבירול"},
         {"IBN|GABIROL", "אבן|גבירול"}},
        {TK_STREET,
         G_RED,
         240,
         150,
         {20, 100, 300, 760, 920, 1100},
         {"ARLOZOROV", "ארלוזורוב"},
         {"ARLOZOROV", "ארלוזורוב"}},
        {TK_TRANS, G_TRANS, 200, 0, {0}, {"LIGHT RAIL", "הרכבת הקלה"}, {"LIGHT|RAIL", "הרכבת|הקלה"}},
        {TK_STREET,
         G_YELLOW,
         260,
         150,
         {22, 110, 330, 800, 980, 1160},
         {"NEVE TZEDEK", "נווה צדק"},
         {"NEVE|TZEDEK", "נווה|צדק"}},
        {TK_STREET, G_YELLOW, 260, 150, {22, 110, 330, 800, 980, 1160}, {"SARONA", "שרונה"}, {"SARONA", "שרונה"}},
        {TK_UTIL, G_UTIL, 150, 0, {0}, {"SCOOTERS", "קורקינטים"}, {"SCOOTERS", "קורקינטים"}},
        {TK_STREET,
         G_YELLOW,
         280,
         150,
         {24, 120, 360, 860, 1040, 1220},
         {"TEL AVIV PORT", "נמל תל אביב"},
         {"TEL AVIV|PORT", "נמל|תל אביב"}},
        {TK_WORKS, G_NONE, 0, 0, {0}, {"ROAD WORKS", "עבודות בכביש"}, {"ROAD|WORKS", "עבודות|בכביש"}},
        {TK_STREET,
         G_GREEN,
         300,
         200,
         {26, 130, 390, 900, 1100, 1280},
         {"THE OLD NORTH", "הצפון הישן"},
         {"OLD|NORTH", "הצפון|הישן"}},
        {TK_STREET,
         G_GREEN,
         300,
         200,
         {26, 130, 390, 900, 1100, 1280},
         {"KIKAR HAMEDINA", "כיכר המדינה"},
         {"KIKAR|HAMEDINA", "כיכר|המדינה"}},
        {TK_CITY, G_NONE, 0, 0, {0}, {"CITY HALL", "העירייה"}, {"CITY HALL", "העירייה"}},
        {TK_STREET,
         G_GREEN,
         320,
         200,
         {28, 150, 440, 1000, 1200, 1400},
         {"RAMAT AVIV", "רמת אביב"},
         {"RAMAT|AVIV", "רמת|אביב"}},
        {TK_TRANS, G_TRANS, 200, 0, {0}, {"SAVIDOR STATION", "תחנת סבידור"}, {"SAVIDOR|STATION", "תחנת|סבידור"}},
        {TK_SURP, G_NONE, 0, 0, {0}, {"SURPRISE", "הפתעה"}, {"SURPRISE", "הפתעה"}},
        {TK_STREET,
         G_DBLUE,
         350,
         200,
         {35, 175, 500, 1100, 1300, 1500},
         {"HABIMA SQUARE", "כיכר הבימה"},
         {"HABIMA|SQUARE", "כיכר|הבימה"}},
        {TK_TAX, G_NONE, 100, 0, {0}, {"PARKING", "חניון"}, {"PARKING", "חניון"}},
        {TK_STREET,
         G_DBLUE,
         400,
         200,
         {50, 200, 600, 1400, 1700, 2000},
         {"ROTHSCHILD BLVD", "שדרות רוטשילד"},
         {"ROTHSCHILD|BLVD", "שדרות|רוטשילד"}},
};

// the transport rent by the count of transport tiles of one owner; the services: the dice times 4 or 10
static const uint16_t TRANS_RENT[5] = {0, 25, 50, 100, 200};

// the colours of the groups (r, g, b)
static const uint8_t GROUP_RGB[NGROUP + 1][3] = {
        {154, 91, 58},  {63, 196, 230}, {255, 111, 174}, {255, 154, 46},  {240, 70, 58},   {255, 210, 63},
        {47, 179, 106}, {47, 79, 216},  {42, 47, 58},    {120, 140, 160}, {200, 192, 178},
};
static const char *const GROUP_NAME[NGROUP][2] = {
        {"BROWN", "חום"},        {"LIGHT BLUE", "תכלת"},  {"PINK", "ורוד"},  {"ORANGE", "כתום"},
        {"RED", "אדום"},         {"YELLOW", "צהוב"},      {"GREEN", "ירוק"}, {"DARK BLUE", "כחול"},
        {"TRANSPORT", "תחבורה"}, {"SERVICES", "שירותים"},
};

// ---- the cards. Deck 0 = CITY HALL (money), deck 1 = SURPRISE (moves and events).
enum {
    CE_GO,
    CE_NEAR_TRANS,
    CE_NEAR_UTIL,
    CE_BACK3,
    CE_JAIL,
    CE_FREE,
    CE_MONEY,
    CE_EACH,
    CE_REPAIRS
};
typedef struct {
    uint8_t effect;
    int16_t a, b;  // CE_GO: the tile; CE_MONEY / CE_EACH: the sum (+ = to the player);
                   // CE_REPAIRS: a floor, a tower
    const char *text[2];
} card_t;
#define NCARD 16
static const card_t CARDS[2][NCARD] = {
        {
                // CITY HALL
                {CE_GO, 0, 0, {"Go to START. Collect ₪200.", "קדימה להתחלה. קבלו 200 ₪."}},
                {CE_MONEY, 200, 0, {"A bank error in your favour. Collect ₪200.", "טעות של הבנק לטובתכם. קבלו 200 ₪."}},
                {CE_MONEY, -50, 0, {"The dentist. Pay ₪50.", "רופא השיניים. משלמים 50 ₪."}},
                {CE_MONEY, 50, 0, {"A sale at the shuk! You save ₪50.", "מבצע בשוק! חסכתם 50 ₪."}},
                {CE_FREE,
                 0,
                 0,
                 {"The city gives you a free scooter ride: get out of the jam. Keep this card.",
                  "העירייה מזמינה נסיעה בקורקינט: יוצאים מהפקק. שמרו את הכרטיס."}},
                {CE_JAIL,
                 0,
                 0,
                 {"Stuck behind a garbage truck! Go to the Ayalon jam. No ₪200.",
                  "תקועים מאחורי משאית זבל! ישר לפקק באיילון, בלי 200 ₪."}},
                {CE_MONEY, 100, 0, {"Your study fund is open. Collect ₪100.", "קרן ההשתלמות נזילה. קבלו 100 ₪."}},
                {CE_MONEY, 20, 0, {"An ARNONA refund. Collect ₪20.", "החזר ארנונה. קבלו 20 ₪."}},
                {CE_EACH,
                 10,
                 0,
                 {"Your birthday party on the roof: collect ₪10 from each player.",
                  "מסיבת יום הולדת על הגג: קבלו 10 ₪ מכל שחקן."}},
                {CE_MONEY, 100, 0, {"Your life insurance pays. Collect ₪100.", "ביטוח החיים משלם. קבלו 100 ₪."}},
                {CE_MONEY, -100, 0, {"A night in the ER. Pay ₪100.", "לילה בחדר המיון. משלמים 100 ₪."}},
                {CE_MONEY, -50, 0, {"The kindergarten fees. Pay ₪50.", "תשלום לגן הילדים. משלמים 50 ₪."}},
                {CE_MONEY, 25, 0, {"A freelance job. Collect ₪25.", "עבודת פרילנס. קבלו 25 ₪."}},
                {CE_REPAIRS,
                 40,
                 115,
                 {"The city fixes the sidewalks: pay ₪40 a floor and ₪115 a tower.",
                  "העירייה מתקנת מדרכות: 40 ₪ לכל קומה ו-115 ₪ לכל מגדל."}},
                {CE_MONEY,
                 10,
                 0,
                 {"Second place in the dog show of the Yarkon Park. Collect ₪10.",
                  "מקום שני בתערוכת הכלבים בפארק הירקון. קבלו 10 ₪."}},
                {CE_MONEY,
                 100,
                 0,
                 {"A great-aunt from Ramat Gan leaves you ₪100.", "דודה רבא מרמת גן השאירה לכם 100 ₪."}},
        },
        {
                // SURPRISE
                {CE_GO, 0, 0, {"Go to START. Collect ₪200.", "קדימה להתחלה. קבלו 200 ₪."}},
                {CE_GO, 39, 0, {"A walk on ROTHSCHILD BLVD.", "טיול בשדרות רוטשילד."}},
                {CE_GO,
                 21,
                 0,
                 {"Meet friends on DIZENGOFF. Pass START? Collect ₪200.",
                  "פגישה עם חברים בדיזנגוף. עברתם בהתחלה? קבלו 200 ₪."}},
                {CE_GO,
                 11,
                 0,
                 {"A coffee on SHEINKIN. Pass START? Collect ₪200.", "קפה בשינקין. עברתם בהתחלה? קבלו 200 ₪."}},
                {CE_NEAR_TRANS,
                 0,
                 0,
                 {"Take the nearest transport. Pay the owner twice the fare.",
                  "לתחבורה הקרובה. משלמים לבעלים פי שניים."}},
                {CE_NEAR_TRANS,
                 0,
                 0,
                 {"Take the nearest transport. Pay the owner twice the fare.",
                  "לתחבורה הקרובה. משלמים לבעלים פי שניים."}},
                {CE_NEAR_UTIL,
                 0,
                 0,
                 {"Go to the nearest service. Pay the owner 10 times the dice.",
                  "לשירות הקרוב. משלמים לבעלים פי 10 מהקוביות."}},
                {CE_MONEY, 150, 0, {"Your startup has an exit! Collect ₪150.", "לסטארט-אפ שלכם יש אקזיט! קבלו 150 ₪."}},
                {CE_FREE,
                 0,
                 0,
                 {"A free scooter ride: get out of the jam. Keep this card.",
                  "נסיעה חינם בקורקינט: יוצאים מהפקק. שמרו את הכרטיס."}},
                {CE_BACK3,
                 0,
                 0,
                 {"The Tel Aviv marathon closes the street. Go back 3.",
                  "מרתון תל אביב סוגר את הרחוב. חוזרים 3 משבצות."}},
                {CE_JAIL,
                 0,
                 0,
                 {"A wrong turn! Go to the Ayalon jam. No ₪200.", "פנייה לא נכונה! ישר לפקק באיילון, בלי 200 ₪."}},
                {CE_REPAIRS,
                 25,
                 100,
                 {"The building committee wants repairs: pay ₪25 a floor and ₪100 a tower.",
                  "ועד הבית רוצה שיפוצים: 25 ₪ לכל קומה ו-100 ₪ לכל מגדל."}},
                {CE_MONEY, -15, 0, {"A parking ticket! Pay ₪15.", "דוח חניה! משלמים 15 ₪."}},
                {CE_GO,
                 25,
                 0,
                 {"Ride the LIGHT RAIL. Pass START? Collect ₪200.", "נסיעה ברכבת הקלה. עברתם בהתחלה? קבלו 200 ₪."}},
                {CE_EACH,
                 -50,
                 0,
                 {"You are the new chair of the building committee: pay each player ₪50.",
                  "נבחרתם ליו״ר ועד הבית: משלמים 50 ₪ לכל שחקן."}},
                {CE_MONEY, 100, 0, {"You win the beach paddle cup! Collect ₪100.", "זכיתם בגביע המטקות! קבלו 100 ₪."}},
        },
};
static const char *const DECK_NAME[2][2] = {{"CITY HALL", "העירייה"}, {"SURPRISE", "הפתעה"}};

// ---- the players: the names and the looks of the bots (the seats of the humans take the name of their board)
#define NLOOK 8
static const char *const BOT_NAME[NLOOK][2] = {
        {"NOA", "נועה"},  {"ITAY", "איתי"}, {"YAEL", "יעל"}, {"BEN", "בן"},
        {"MAYA", "מאיה"}, {"AVI", "אבי"},   {"DANA", "דנה"}, {"YONI", "יוני"},
};
// the colour of each seat (the token, the panel)
static const uint8_t SEAT_RGB[MAXP][3] = {
        {59, 140, 255}, {255, 138, 42}, {47, 194, 122}, {255, 95, 168}, {166, 107, 255}, {40, 200, 210},
};
// a look: the skin, the hair, the style of the hair (0 long, 1 curls, 2 bun, 3 cap, 4 short, 5 pony)
static const uint8_t LOOK_SKIN[NLOOK][3] = {
        {242, 199, 165}, {217, 160, 122}, {246, 210, 184}, {185, 122, 85},
        {230, 180, 140}, {160, 105, 72},  {250, 214, 190}, {205, 150, 110},
};
static const uint8_t LOOK_HAIR[NLOOK][3] = {
        {43, 29, 22},  {27, 20, 16}, {194, 70, 31},  {21, 21, 21},
        {120, 72, 30}, {30, 30, 34}, {232, 190, 90}, {60, 40, 30},
};
static const uint8_t LOOK_STYLE[NLOOK] = {0, 1, 2, 3, 5, 4, 0, 1};
