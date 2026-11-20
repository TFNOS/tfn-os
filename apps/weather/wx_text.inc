// WEATHER: the texts in English and Hebrew (TX(a) = the text of the current language). A Hebrew text never starts
// with a number, and a Hebrew letter never touches a number (the line goes from the right: "ב-20" shows wrong).
// The task: included by app_weather.c; the drawing of the app task reads the tables.
// The memory: constant tables in the app file.
#define TX(a) ((a)[s_he])
typedef const char *const tx_t[2];

// the conditions (the WMO codes of Open-Meteo) and their icons
enum {
    IC_SUN,
    IC_MOON,
    IC_FEW_D,
    IC_FEW_N,
    IC_PART_D,
    IC_PART_N,
    IC_CLOUD,
    IC_FOG,
    IC_DRIZZLE,
    IC_RAIN,
    IC_HEAVY,
    IC_SHOWER_D,
    IC_SHOWER_N,
    IC_SNOW,
    IC_SLEET,
    IC_THUNDER,
    IC_N
};
// the sky of a condition: the look of the live sky
enum {
    SK_CLEAR,
    SK_FEW,
    SK_PART,
    SK_CLOUD,
    SK_FOG,
    SK_DRIZZLE,
    SK_RAIN,
    SK_HEAVY,
    SK_SNOW,
    SK_STORM
};
typedef struct {
    int code;
    const char *en, *he, *en_n;
    uint8_t ic_d, ic_n, sky;
} cond_t;
static const cond_t CONDS[] = {
        {0, "Sunny", "שמשי", "Clear", IC_SUN, IC_MOON, SK_CLEAR},
        {1, "Mostly clear", "בהיר ברובו", "Mostly clear", IC_FEW_D, IC_FEW_N, SK_FEW},
        {2, "Partly cloudy", "מעונן חלקית", "Partly cloudy", IC_PART_D, IC_PART_N, SK_PART},
        {3, "Cloudy", "מעונן", "Cloudy", IC_CLOUD, IC_CLOUD, SK_CLOUD},
        {45, "Fog", "ערפל", "Fog", IC_FOG, IC_FOG, SK_FOG},
        {48, "Freezing fog", "ערפל קופא", "Freezing fog", IC_FOG, IC_FOG, SK_FOG},
        {51, "Light drizzle", "טפטוף קל", "Light drizzle", IC_DRIZZLE, IC_DRIZZLE, SK_DRIZZLE},
        {53, "Drizzle", "טפטוף", "Drizzle", IC_DRIZZLE, IC_DRIZZLE, SK_DRIZZLE},
        {55, "Heavy drizzle", "טפטוף כבד", "Heavy drizzle", IC_RAIN, IC_RAIN, SK_RAIN},
        {56, "Freezing drizzle", "טפטוף קופא", "Freezing drizzle", IC_SLEET, IC_SLEET, SK_DRIZZLE},
        {57, "Freezing drizzle", "טפטוף קופא", "Freezing drizzle", IC_SLEET, IC_SLEET, SK_DRIZZLE},
        {61, "Light rain", "גשם קל", "Light rain", IC_DRIZZLE, IC_DRIZZLE, SK_DRIZZLE},
        {63, "Rain", "גשם", "Rain", IC_RAIN, IC_RAIN, SK_RAIN},
        {65, "Heavy rain", "גשם כבד", "Heavy rain", IC_HEAVY, IC_HEAVY, SK_HEAVY},
        {66, "Freezing rain", "גשם קופא", "Freezing rain", IC_SLEET, IC_SLEET, SK_RAIN},
        {67, "Freezing rain", "גשם קופא", "Freezing rain", IC_SLEET, IC_SLEET, SK_HEAVY},
        {71, "Light snow", "שלג קל", "Light snow", IC_SNOW, IC_SNOW, SK_SNOW},
        {73, "Snow", "שלג", "Snow", IC_SNOW, IC_SNOW, SK_SNOW},
        {75, "Heavy snow", "שלג כבד", "Heavy snow", IC_SNOW, IC_SNOW, SK_SNOW},
        {77, "Snow grains", "גרגרי שלג", "Snow grains", IC_SNOW, IC_SNOW, SK_SNOW},
        {80, "Light showers", "ממטרים קלים", "Light showers", IC_SHOWER_D, IC_SHOWER_N, SK_DRIZZLE},
        {81, "Showers", "ממטרים", "Showers", IC_SHOWER_D, IC_SHOWER_N, SK_RAIN},
        {82, "Heavy showers", "ממטרים עזים", "Heavy showers", IC_HEAVY, IC_HEAVY, SK_HEAVY},
        {85, "Snow showers", "ממטרי שלג", "Snow showers", IC_SNOW, IC_SNOW, SK_SNOW},
        {86, "Snow showers", "ממטרי שלג", "Snow showers", IC_SNOW, IC_SNOW, SK_SNOW},
        {95, "Thunderstorm", "סופת רעמים", "Thunderstorm", IC_THUNDER, IC_THUNDER, SK_STORM},
        {96, "Storm and hail", "סופת רעמים וברד", "Storm and hail", IC_THUNDER, IC_THUNDER, SK_STORM},
        {99, "Storm and hail", "סופת רעמים וברד", "Storm and hail", IC_THUNDER, IC_THUNDER, SK_STORM},
};
static const cond_t *cond_of(int code)
{
    for (unsigned i = 0; i < sizeof CONDS / sizeof CONDS[0]; i++) {
        if (CONDS[i].code == code) {
            return &CONDS[i];
        }
    }
    // (an unknown code: the nearest lower code)
    const cond_t *best = &CONDS[0];
    for (unsigned i = 0; i < sizeof CONDS / sizeof CONDS[0]; i++) {
        if (CONDS[i].code <= code) {
            best = &CONDS[i];
        }
    }
    return best;
}

static tx_t T_FEELS = {"FEELS LIKE", "מרגיש כמו"};
static tx_t T_HUMID = {"HUMIDITY", "לחות"};
static tx_t T_WIND = {"WIND", "רוח"};
static tx_t T_GUSTS = {"GUSTS", "משבים"};
static tx_t T_UV = {"UV INDEX", "קרינה"};
static tx_t T_SUN = {"SUN", "שמש"};
static tx_t T_RISE = {"SUNRISE", "זריחה"};
static tx_t T_SET = {"SUNSET", "שקיעה"};
static tx_t T_PRESS = {"PRESSURE", "לחץ אוויר"};
static tx_t T_VIS = {"VISIBILITY", "ראות"};
static tx_t T_AIR = {"AIR", "אוויר"};
static tx_t T_MOON = {"MOON", "ירח"};
static tx_t T_DEW = {"DEW", "טל"};
static tx_t T_HOURS = {"NEXT HOURS", "השעות הקרובות"};
static tx_t T_DAYS = {"TEN DAYS", "עשרה ימים"};
static tx_t T_NEXTDAYS = {"NEXT DAYS", "הימים הבאים"};
static tx_t T_RAIN = {"RAIN", "גשם"};
static tx_t T_NOW = {"NOW", "עכשיו"};
static tx_t T_TODAY = {"TODAY", "היום"};
static tx_t T_HIGH = {"H", "מקס׳"};
static tx_t T_LOW = {"L", "מינ׳"};
static tx_t T_UPD = {"UPDATED", "עודכן בשעה"};
static tx_t T_UPDATING = {"UPDATING", "מעדכן"};
static tx_t T_NOUPD = {"COULD NOT UPDATE", "העדכון נכשל"};
static tx_t T_NOWIFI = {"NO WIFI", "אין חיבור לרשת"};
static tx_t T_WIFI1 = {"CONNECT THE BOARD", "חברו את הלוח לרשת"};  // (the two lines of the way to fix it)
static tx_t T_WIFI2 = {"IN SETUP > WIFI", "במסך WIFI של SETUP"};
static tx_t T_OLD = {"THE DATA IS OLD", "הנתונים ישנים"};
static tx_t T_FINDING = {"FINDING YOUR PLACE", "מחפש את המקום שלך"};
static tx_t T_LOADING = {"GETTING THE WEATHER", "מביא את מזג האוויר"};
static tx_t T_PULL = {"PULL TO UPDATE", "משכו כדי לעדכן"};
static tx_t T_RELEASE = {"RELEASE TO UPDATE", "שחררו כדי לעדכן"};
// The credit of the data (the licence CC BY 4.0 of Open-Meteo): the same English line in both languages. A narrow
// place shows it in two lines (T_CREDIT1, T_CREDIT2).
static const char T_CREDIT[] = "Weather data by Open-Meteo.com";
static const char T_CREDIT1[] = "Weather data by";
static const char T_CREDIT2[] = "Open-Meteo.com";
static tx_t T_PLACES = {"PLACES", "מקומות"};
static tx_t T_MYPLACE = {"MY PLACE", "המקום שלי"};
static tx_t T_ADD = {"ADD A PLACE", "הוספת מקום"};
static tx_t T_SEARCH = {"SEARCH", "חיפוש"};
static tx_t T_TYPE = {"TYPE THE NAME OF A CITY", "הקלידו שם של עיר"};
static tx_t T_TURN = {"TURN THE BOARD TO TYPE", "סובבו את הלוח כדי להקליד"};
static tx_t T_HOLD = {"HOLD A PLACE TO REMOVE IT", "החזיקו מקום כדי להסיר אותו"};
static tx_t T_NORES = {"NO PLACE WITH THIS NAME", "אין מקום בשם הזה"};
static tx_t T_SEARCHING = {"SEARCHING", "מחפש"};
static tx_t T_FULL = {"8 PLACES AT MOST", "שמונה מקומות לכל היותר"};
static tx_t T_BACK = {"BACK", "חזרה"};
static tx_t T_ISRAEL = {"ISRAEL", "ישראל"};
static tx_t T_WORLD = {"WORLD", "העולם"};
static tx_t T_NORAIN = {"NO RAIN IN THE NEXT HOURS", "אין גשם בשעות הקרובות"};
static tx_t T_RAINNOW = {"RAIN NOW", "יורד גשם"};
static tx_t T_RAININ = {"RAIN IN %d MIN", "גשם בעוד %d דקות"};
static tx_t T_RAINSTOP = {"THE RAIN STOPS IN %d MIN", "הגשם ייפסק בעוד %d דקות"};
static tx_t T_RAINLONG = {"RAIN FOR THE NEXT HOURS", "גשם בשעות הקרובות"};
static tx_t T_MIN = {"MIN", "דק׳"};
static tx_t T_KM = {"KM", "ק״מ"};

static tx_t DAYS[7] = {{"SUN", "ראשון"}, {"MON", "שני"},  {"TUE", "שלישי"}, {"WED", "רביעי"},
                       {"THU", "חמישי"}, {"FRI", "שישי"}, {"SAT", "שבת"}};
static tx_t UVL[5] = {{"LOW", "נמוכה"},
                      {"MODERATE", "בינונית"},
                      {"HIGH", "גבוהה"},
                      {"VERY HIGH", "גבוהה מאוד"},
                      {"EXTREME", "קיצונית"}};
static tx_t AQL[6] = {{"GOOD", "טובה"},          {"MODERATE", "סבירה"},       {"POOR", "ירודה"},
                      {"UNHEALTHY", "לא בריאה"}, {"VERY POOR", "גרועה מאוד"}, {"HAZARDOUS", "מסוכנת"}};
static tx_t MOONS[8] = {{"NEW MOON", "מולד"},           {"WAXING CRESCENT", "סהר עולה"},
                        {"FIRST QUARTER", "רבע ראשון"}, {"WAXING GIBBOUS", "ירח מתמלא"},
                        {"FULL MOON", "ירח מלא"},       {"WANING GIBBOUS", "ירח מתמעט"},
                        {"LAST QUARTER", "רבע אחרון"},  {"WANING CRESCENT", "סהר יורד"}};
static tx_t DIRS[8] = {{"N", "צפון"}, {"NE", "צפון מזרח"}, {"E", "מזרח"}, {"SE", "דרום מזרח"},
                       {"S", "דרום"}, {"SW", "דרום מערב"}, {"W", "מערב"}, {"NW", "צפון מערב"}};

// the panel of the app
static const char *const UNIT_VALS[] = {"\xc2\xb0"
                                        "C",
                                        "\xc2\xb0"
                                        "C",
                                        "\xc2\xb0"
                                        "F",
                                        "\xc2\xb0"
                                        "F"};
static const char *const WUNIT_VALS[] = {"KM/H", "קמ״ש", "M/S", "מ׳ לשנייה", "MPH", "מייל לשעה", "KNOTS", "קשרים"};
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const MOTION_VALS[] = {"ON", "פועלת", "OFF", "כבויה"};
static tx_t WUNIT_SHORT[4] = {{"KM/H", "קמ״ש"}, {"M/S", "מ׳/ש׳"}, {"MPH", "מייל"}, {"KN", "קשר"}};

// places to add with no keyboard (the names in both languages)
typedef struct {
    const char *en, *he;
    float lat, lon;
    bool il;
} preset_t;
static const preset_t PRESETS[] = {
        {"Tel Aviv", "תל אביב", 32.0853f, 34.7818f, true},
        {"Jerusalem", "ירושלים", 31.7683f, 35.2137f, true},
        {"Haifa", "חיפה", 32.7940f, 34.9896f, true},
        {"Be'er Sheva", "באר שבע", 31.2518f, 34.7913f, true},
        {"Eilat", "אילת", 29.5577f, 34.9519f, true},
        {"Tiberias", "טבריה", 32.7922f, 35.5312f, true},
        {"Netanya", "נתניה", 32.3215f, 34.8532f, true},
        {"Ashdod", "אשדוד", 31.8044f, 34.6553f, true},
        {"Rishon LeZion", "ראשון לציון", 31.9730f, 34.7925f, true},
        {"Ramat Gan", "רמת גן", 32.0684f, 34.8248f, true},
        {"Herzliya", "הרצליה", 32.1624f, 34.8447f, true},
        {"Safed", "צפת", 32.9646f, 35.4960f, true},
        {"Mitzpe Ramon", "מצפה רמון", 30.6100f, 34.8017f, true},
        {"Mount Hermon", "החרמון", 33.3100f, 35.7720f, true},
        {"London", "לונדון", 51.5074f, -0.1278f, false},
        {"Paris", "פריז", 48.8566f, 2.3522f, false},
        {"New York", "ניו יורק", 40.7128f, -74.0060f, false},
        {"Tokyo", "טוקיו", 35.6762f, 139.6503f, false},
        {"Rome", "רומא", 41.9028f, 12.4964f, false},
        {"Berlin", "ברלין", 52.5200f, 13.4050f, false},
        {"Athens", "אתונה", 37.9838f, 23.7275f, false},
        {"Dubai", "דובאי", 25.2048f, 55.2708f, false},
        {"Bangkok", "בנגקוק", 13.7563f, 100.5018f, false},
        {"Sydney", "סידני", -33.8688f, 151.2093f, false},
        {"Reykjavik", "רייקיאוויק", 64.1466f, -21.9426f, false},
        {"Los Angeles", "לוס אנג׳לס", 34.0522f, -118.2437f, false},
};
#define NPRESET ((int)(sizeof PRESETS / sizeof PRESETS[0]))
