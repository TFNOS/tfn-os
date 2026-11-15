// HEBREW CALENDAR: the cities (the places of geonames.org, the zone as a POSIX TZ text of 2026). israel: the holidays
// of Israel; cmin: the candle minutes before the sunset (the rules of hebcal.com: Jerusalem 40, Haifa 30, the other
// cities of Israel 20, the Diaspora 18). The English names of the cities that CLOCK has too are the same (the home
// city of CLOCK is the first choice here).
typedef struct {
    const char *name[2];  // English, Hebrew
    float lat, lon;       // degrees (north, east)
    const char *tz;
    uint8_t israel, cmin;
} hc_city_t;
#define TZ_IL "IST-2IDT,M3.4.4/26,M10.5.0"
#define TZ_NY "EST5EDT,M3.2.0,M11.1.0"
#define TZ_EU "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_AU "AEST-10AEDT,M10.1.0,M4.1.0/3"
static const hc_city_t HC_CITIES[] = {
        {{"JERUSALEM", "ירושלים"}, 31.76904f, 35.21633f, TZ_IL, 1, 40},
        {{"TEL AVIV", "תל אביב"}, 32.08088f, 34.78057f, TZ_IL, 1, 20},
        {{"HAIFA", "חיפה"}, 32.81841f, 34.98850f, TZ_IL, 1, 30},
        {{"BE'ER SHEVA", "באר שבע"}, 31.25181f, 34.79130f, TZ_IL, 1, 20},
        {{"EILAT", "אילת"}, 29.55805f, 34.94821f, TZ_IL, 1, 20},
        {{"TIBERIAS", "טבריה"}, 32.79221f, 35.53124f, TZ_IL, 1, 20},
        {{"SAFED", "צפת"}, 32.96465f, 35.49600f, TZ_IL, 1, 20},
        {{"NETANYA", "נתניה"}, 32.33291f, 34.85992f, TZ_IL, 1, 20},
        {{"ASHDOD", "אשדוד"}, 31.79213f, 34.64966f, TZ_IL, 1, 20},
        {{"ASHKELON", "אשקלון"}, 31.66926f, 34.57149f, TZ_IL, 1, 20},
        {{"RISHON LEZION", "ראשון לציון"}, 31.96400f, 34.80444f, TZ_IL, 1, 20},
        {{"PETAH TIKVA", "פתח תקווה"}, 32.08707f, 34.88747f, TZ_IL, 1, 20},
        {{"BNEI BRAK", "בני ברק"}, 32.08074f, 34.83380f, TZ_IL, 1, 20},
        {{"RAMAT GAN", "רמת גן"}, 32.08227f, 34.81065f, TZ_IL, 1, 20},
        {{"HOLON", "חולון"}, 32.01034f, 34.77918f, TZ_IL, 1, 20},
        {{"REHOVOT", "רחובות"}, 31.89421f, 34.81199f, TZ_IL, 1, 20},
        {{"MODI'IN", "מודיעין"}, 31.89670f, 35.01033f, TZ_IL, 1, 20},
        {{"RA'ANANA", "רעננה"}, 32.18440f, 34.87076f, TZ_IL, 1, 20},
        {{"HERZLIYA", "הרצליה"}, 32.16627f, 34.82536f, TZ_IL, 1, 20},
        {{"KFAR SABA", "כפר סבא"}, 32.17500f, 34.90690f, TZ_IL, 1, 20},
        {{"HADERA", "חדרה"}, 32.44192f, 34.92302f, TZ_IL, 1, 20},
        {{"BEIT SHEMESH", "בית שמש"}, 31.73072f, 34.99293f, TZ_IL, 1, 20},
        {{"NAZARETH", "נצרת"}, 32.70056f, 35.29722f, TZ_IL, 1, 20},
        {{"AFULA", "עפולה"}, 32.60907f, 35.28920f, TZ_IL, 1, 20},
        {{"KARMIEL", "כרמיאל"}, 32.91708f, 35.30501f, TZ_IL, 1, 20},
        {{"KIRYAT SHMONA", "קריית שמונה"}, 33.20733f, 35.57212f, TZ_IL, 1, 20},
        {{"DIMONA", "דימונה"}, 31.07079f, 35.03269f, TZ_IL, 1, 20},
        {{"ARIEL", "אריאל"}, 32.10610f, 35.18730f, TZ_IL, 1, 20},
        {{"NEW YORK", "ניו יורק"}, 40.71427f, -74.00597f, TZ_NY, 0, 18},
        {{"BOSTON", "בוסטון"}, 42.35843f, -71.05977f, TZ_NY, 0, 18},
        {{"MIAMI", "מיאמי"}, 25.77427f, -80.19366f, TZ_NY, 0, 18},
        {{"TORONTO", "טורונטו"}, 43.70011f, -79.41630f, TZ_NY, 0, 18},
        {{"MONTREAL", "מונטריאול"}, 45.50884f, -73.58781f, TZ_NY, 0, 18},
        {{"CHICAGO", "שיקגו"}, 41.85003f, -87.65005f, "CST6CDT,M3.2.0,M11.1.0", 0, 18},
        {{"LOS ANGELES", "לוס אנג'לס"}, 34.05223f, -118.24368f, "PST8PDT,M3.2.0,M11.1.0", 0, 18},
        {{"MEXICO CITY", "מקסיקו סיטי"}, 19.42847f, -99.12766f, "CST6", 0, 18},
        {{"BUENOS AIRES", "בואנוס איירס"}, -34.61315f, -58.37723f, "<-03>3", 0, 18},
        {{"SAO PAULO", "סאו פאולו"}, -23.54750f, -46.63611f, "<-03>3", 0, 18},
        {{"LONDON", "לונדון"}, 51.50853f, -0.12574f, "GMT0BST,M3.5.0/1,M10.5.0", 0, 18},
        {{"PARIS", "פריז"}, 48.85341f, 2.34880f, TZ_EU, 0, 18},
        {{"ANTWERP", "אנטוורפן"}, 51.21989f, 4.40346f, TZ_EU, 0, 18},
        {{"AMSTERDAM", "אמסטרדם"}, 52.37403f, 4.88969f, TZ_EU, 0, 18},
        {{"BERLIN", "ברלין"}, 52.52437f, 13.41053f, TZ_EU, 0, 18},
        {{"ZURICH", "ציריך"}, 47.36667f, 8.55000f, TZ_EU, 0, 18},
        {{"ROME", "רומא"}, 41.89193f, 12.51133f, TZ_EU, 0, 18},
        {{"BUDAPEST", "בודפשט"}, 47.49835f, 19.04045f, TZ_EU, 0, 18},
        {{"KYIV", "קייב"}, 50.45466f, 30.52380f, "EET-2EEST,M3.5.0/3,M10.5.0/4", 0, 18},
        {{"MOSCOW", "מוסקבה"}, 55.75222f, 37.61556f, "MSK-3", 0, 18},
        {{"JOHANNESBURG", "יוהנסבורג"}, -26.20227f, 28.04363f, "SAST-2", 0, 18},
        {{"SYDNEY", "סידני"}, -33.86785f, 151.20732f, TZ_AU, 0, 18},
        {{"MELBOURNE", "מלבורן"}, -37.81400f, 144.96332f, TZ_AU, 0, 18},
};
#define HC_NCITY        ((int)(sizeof(HC_CITIES) / sizeof(HC_CITIES[0])))
#define HC_CITY_DEFAULT 1  // (TEL AVIV)
