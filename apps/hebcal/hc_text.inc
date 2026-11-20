// HEBREW CALENDAR: the texts (English, Hebrew). TX(T_x) gives the text of the language of the app.
enum {
    T_BACK,
    T_CALENDAR,
    T_TIMES,
    T_TODAY,
    T_PREV,
    T_NEXT,
    T_CANDLES,
    T_CANDLES_NIGHT,
    T_HAVDALAH,
    T_FAST_BEGINS,
    T_FAST_ENDS,
    T_NOTSET,
    T_NOTSET2,
    T_SHABBAT,
    T_IN_DAYS,
    T_TOMORROW_IS,
    T_TONIGHT,
    T_CHOOSE_CITY,
    T_ISRAEL,
    T_DIASPORA,
    T_DAY_N,
    T_OMER_N,
    T_CANDLE_N,
    T_CANDLE_1,
    T_THIS_WEEK,
    T_NONE,
    T_N
};
static const char *const TEXTS[T_N][2] = {
        {"BACK", "חזרה"},
        {"CALENDAR", "לוח שנה"},
        {"TIMES", "זמנים"},
        {"TODAY", "היום"},
        {"DAY BEFORE", "יום קודם"},
        {"DAY AFTER", "יום הבא"},
        {"CANDLE LIGHTING", "הדלקת נרות"},
        {"CANDLES AFTER", "הדלקת נרות אחרי"},
        {"HAVDALAH", "הבדלה"},
        {"FAST BEGINS", "תחילת הצום"},
        {"FAST ENDS", "סוף הצום"},
        {"THE CLOCK IS NOT SET", "השעון לא מכוון"},
        {"CONNECT TO WIFI: THE TIME COMES FROM THE NETWORK", "התחברו ל-WIFI: השעה מגיעה מהרשת"},
        {"SHABBAT", "שבת"},
        {"%s IN %d DAYS", "%s בעוד %d ימים"},
        {"TOMORROW: %s", "מחר: %s"},
        {"FROM SUNSET: %s", "מהשקיעה: %s"},
        {"CHOOSE A CITY", "בחירת עיר"},
        {"ISRAEL", "ישראל"},
        {"DIASPORA", "חוץ לארץ"},
        {"%s, DAY %d", "%s, יום %d"},
        {"OMER: DAY %d", "העומר: יום %d"},
        {"%d CANDLES TONIGHT", "%d נרות הערב"},
        {"CHANUKAH: THE FIRST CANDLE TONIGHT", "חנוכה: הנר הראשון הערב"},
        {"THIS WEEK", "השבוע"},
        {"NO HOLIDAY", "אין חג"},
};
#define TX(i) TEXTS[i][s_he]

// the events (hc_cal.inc HE_*)
static const char *const EV_NAME[HE_N][2] = {
        [HE_EREV_RH] = {"EREV ROSH HASHANA", "ערב ראש השנה"},
        [HE_RH] = {"ROSH HASHANA", "ראש השנה"},
        [HE_GEDALIAH] = {"TZOM GEDALIAH", "צום גדליה"},
        [HE_SHUVA] = {"SHABBAT SHUVA", "שבת שובה"},
        [HE_EREV_YK] = {"EREV YOM KIPPUR", "ערב יום כיפור"},
        [HE_YK] = {"YOM KIPPUR", "יום כיפור"},
        [HE_EREV_SUKKOT] = {"EREV SUKKOT", "ערב סוכות"},
        [HE_SUKKOT] = {"SUKKOT", "סוכות"},
        [HE_SUKKOT_CHM] = {"CHOL HAMOED SUKKOT", "חול המועד סוכות"},
        [HE_HOSHANA] = {"HOSHANA RABA", "הושענא רבה"},
        [HE_SHMINI] = {"SHMINI ATZERET", "שמיני עצרת"},
        [HE_SIMCHAT] = {"SIMCHAT TORAH", "שמחת תורה"},
        [HE_CHANUKAH] = {"CHANUKAH", "חנוכה"},
        [HE_TEVET10] = {"ASARA B'TEVET", "עשרה בטבת"},
        [HE_TUBISHVAT] = {"TU BISHVAT", "ט\xD7\xB4ו בשבט"},
        [HE_SHIRAH] = {"SHABBAT SHIRAH", "שבת שירה"},
        [HE_SHEKALIM] = {"SHABBAT SHEKALIM", "שבת שקלים"},
        [HE_ZACHOR] = {"SHABBAT ZACHOR", "שבת זכור"},
        [HE_ESTHER] = {"TA'ANIT ESTHER", "תענית אסתר"},
        [HE_PURIM] = {"PURIM", "פורים"},
        [HE_SHUSHAN] = {"SHUSHAN PURIM", "שושן פורים"},
        [HE_PURIM_KATAN] = {"PURIM KATAN", "פורים קטן"},
        [HE_PARAH] = {"SHABBAT PARAH", "שבת פרה"},
        [HE_HACHODESH] = {"SHABBAT HACHODESH", "שבת החודש"},
        [HE_HAGADOL] = {"SHABBAT HAGADOL", "שבת הגדול"},
        [HE_EREV_PESACH] = {"EREV PESACH", "ערב פסח"},
        [HE_PESACH] = {"PESACH", "פסח"},
        [HE_PESACH_CHM] = {"CHOL HAMOED PESACH", "חול המועד פסח"},
        [HE_SHOAH] = {"YOM HASHOAH", "יום השואה"},
        [HE_ZIKARON] = {"YOM HAZIKARON", "יום הזיכרון"},
        [HE_ATZMAUT] = {"YOM HAATZMAUT", "יום העצמאות"},
        [HE_PESACH_SHENI] = {"PESACH SHENI", "פסח שני"},
        [HE_LAG] = {"LAG BAOMER", "ל\xD7\xB4ג בעומר"},
        [HE_YERUSHALAYIM] = {"YOM YERUSHALAYIM", "יום ירושלים"},
        [HE_EREV_SHAVUOT] = {"EREV SHAVUOT", "ערב שבועות"},
        [HE_SHAVUOT] = {"SHAVUOT", "שבועות"},
        [HE_TAMMUZ17] = {"TZOM TAMMUZ", "צום י\xD7\xB4ז בתמוז"},
        [HE_CHAZON] = {"SHABBAT CHAZON", "שבת חזון"},
        [HE_AV9] = {"TISHA B'AV", "תשעה באב"},
        [HE_NACHAMU] = {"SHABBAT NACHAMU", "שבת נחמו"},
        [HE_TUBEAV] = {"TU B'AV", "ט\xD7\xB4ו באב"},
        [HE_RC] = {"ROSH CHODESH", "ראש חודש"},
        [HE_OMER] = {"OMER", "העומר"},
        [HE_BECHOROT] = {"TA'ANIT BECHOROT", "תענית בכורות"},
};

// the Hebrew months (1 = Nisan .. 13 = Adar II); [0]: Adar I of a leap year
static const char *const HMONTH[14][2] = {
        {"ADAR I", "אדר א\xD7\xB3"},
        {"NISAN", "ניסן"},
        {"IYAR", "אייר"},
        {"SIVAN", "סיון"},
        {"TAMMUZ", "תמוז"},
        {"AV", "אב"},
        {"ELUL", "אלול"},
        {"TISHREI", "תשרי"},
        {"CHESHVAN", "חשון"},
        {"KISLEV", "כסלו"},
        {"TEVET", "טבת"},
        {"SHVAT", "שבט"},
        {"ADAR", "אדר"},
        {"ADAR II", "אדר ב\xD7\xB3"},
};
static const char *hmonth(int y, int m)
{
    return HMONTH[m == 12 && hc_leap(y) ? 0 : m][s_he];
}

static const char *const GMONTH[12][2] = {
        {"JANUARY", "ינואר"},    {"FEBRUARY", "פברואר"}, {"MARCH", "מרץ"},       {"APRIL", "אפריל"},
        {"MAY", "מאי"},          {"JUNE", "יוני"},       {"JULY", "יולי"},       {"AUGUST", "אוגוסט"},
        {"SEPTEMBER", "ספטמבר"}, {"OCTOBER", "אוקטובר"}, {"NOVEMBER", "נובמבר"}, {"DECEMBER", "דצמבר"},
};
static const char *const GMON3[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char *const WDAY[7][2] = {
        {"SUNDAY", "יום ראשון"},   {"MONDAY", "יום שני"},  {"TUESDAY", "יום שלישי"}, {"WEDNESDAY", "יום רביעי"},
        {"THURSDAY", "יום חמישי"}, {"FRIDAY", "יום שישי"}, {"SATURDAY", "שבת"},
};
static const char *const WDAY_S[7][2] = {
        {"SUN", "א\xD7\xB3"}, {"MON", "ב\xD7\xB3"}, {"TUE", "ג\xD7\xB3"}, {"WED", "ד\xD7\xB3"},
        {"THU", "ה\xD7\xB3"}, {"FRI", "ו\xD7\xB3"}, {"SAT", "ש\xD7\xB3"},
};

// the times of the day (hc_cal.inc HZ_*)
static const char *const ZNAME[HZ_N][2] = {
        {"DAWN", "עלות השחר"},
        {"SUNRISE", "הנץ החמה"},
        {"LATEST SHEMA", "סוף זמן ק\xD7\xB4ש"},
        {"LATEST TEFILLA", "סוף זמן תפילה"},
        {"MIDDAY", "חצות היום"},
        {"MINCHA GEDOLA", "מנחה גדולה"},
        {"PLAG HAMINCHA", "פלג המנחה"},
        {"SUNSET", "שקיעה"},
        {"NIGHTFALL", "צאת הכוכבים"},
};

// the weekly readings (pyluach order; hc_parsha.inc codes)
static const char *const PARSHA[54][2] = {
        {"BERESHIT", "בראשית"},
        {"NOACH", "נח"},
        {"LECH LECHA", "לך לך"},
        {"VAYERA", "וירא"},
        {"CHAYEI SARAH", "חיי שרה"},
        {"TOLDOT", "תולדות"},
        {"VAYETZE", "ויצא"},
        {"VAYISHLACH", "וישלח"},
        {"VAYESHEV", "וישב"},
        {"MIKETZ", "מקץ"},
        {"VAYIGASH", "ויגש"},
        {"VAYECHI", "ויחי"},
        {"SHEMOT", "שמות"},
        {"VAERA", "וארא"},
        {"BO", "בא"},
        {"BESHALACH", "בשלח"},
        {"YITRO", "יתרו"},
        {"MISHPATIM", "משפטים"},
        {"TERUMAH", "תרומה"},
        {"TETZAVEH", "תצוה"},
        {"KI TISA", "כי תשא"},
        {"VAYAKHEL", "ויקהל"},
        {"PEKUDEI", "פקודי"},
        {"VAYIKRA", "ויקרא"},
        {"TZAV", "צו"},
        {"SHMINI", "שמיני"},
        {"TAZRIA", "תזריע"},
        {"METZORA", "מצורע"},
        {"ACHAREI MOT", "אחרי מות"},
        {"KEDOSHIM", "קדושים"},
        {"EMOR", "אמור"},
        {"BEHAR", "בהר"},
        {"BECHUKOTAI", "בחוקותי"},
        {"BAMIDBAR", "במדבר"},
        {"NASO", "נשא"},
        {"BEHAALOTCHA", "בהעלותך"},
        {"SHLACH", "שלח"},
        {"KORACH", "קרח"},
        {"CHUKAT", "חקת"},
        {"BALAK", "בלק"},
        {"PINCHAS", "פינחס"},
        {"MATOT", "מטות"},
        {"MASEI", "מסעי"},
        {"DEVARIM", "דברים"},
        {"VAETCHANAN", "ואתחנן"},
        {"EKEV", "עקב"},
        {"RE'EH", "ראה"},
        {"SHOFTIM", "שופטים"},
        {"KI TETZE", "כי תצא"},
        {"KI TAVO", "כי תבוא"},
        {"NITZAVIM", "נצבים"},
        {"VAYELECH", "וילך"},
        {"HAAZINU", "האזינו"},
        {"VEZOT HABRACHA", "וזאת הברכה"},
};
// "PARASHAT VAYERA", "VAYAKHEL-PEKUDEI"; the code of hc_parsha() (0: "")
static void parsha_text(int code, char *o, int n)
{
    if (code <= 0) {
        o[0] = 0;
        return;
    }
    if (code > 100) {
        snprintf(o, (size_t)n, "%s-%s", PARSHA[code - 101][s_he], PARSHA[code - 100][s_he]);
    } else {
        snprintf(o, (size_t)n, "%s", PARSHA[code - 1][s_he]);
    }
}

// the name of an event ("SUKKOT II", "ROSH CHODESH KISLEV", "CHANUKAH, DAY 3")
static void ev_text(const hc_ev_t *e, int y, char *o, int n)
{
    const char *b = EV_NAME[e->id][s_he];
    static const char *const ROMAN[9] = {"", "", " II", " III", " IV", " V", " VI", " VII", " VIII"};
    static const char *const HNUM[9] = {"",           "",           " ב\xD7\xB3", " ג\xD7\xB3", " ד\xD7\xB3",
                                        " ה\xD7\xB3", " ו\xD7\xB3", " ז\xD7\xB3", " ח\xD7\xB3"};
    switch (e->id) {
    case HE_RH:
    case HE_SUKKOT:
    case HE_PESACH:
    case HE_SHAVUOT:
        if (e->id == HE_PESACH && e->n == 7) {
            snprintf(o, (size_t)n, "%s", s_he ? "שביעי של פסח" : "PESACH VII");
        } else if (e->id == HE_PESACH && e->n == 8) {
            snprintf(o, (size_t)n, "%s", s_he ? "אחרון של פסח" : "PESACH VIII");
        } else {
            snprintf(o, (size_t)n, "%s%s", b, (s_he ? HNUM : ROMAN)[e->n < 9 ? e->n : 0]);
        }
        break;
    case HE_CHANUKAH:
        snprintf(o, (size_t)n, TX(T_DAY_N), b, e->n);
        break;
    case HE_OMER:
        snprintf(o, (size_t)n, TX(T_OMER_N), e->n);
        break;
    case HE_RC:
        snprintf(o, (size_t)n, "%s %s", b, hmonth(y, e->n));
        break;
    default:
        snprintf(o, (size_t)n, "%s", b);
        break;
    }
}
