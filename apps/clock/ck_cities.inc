// CLOCK: the cities of the world clock with the POSIX TZ text of their zone (the rules of 2026). tz.c (tfn/tz.h)
// reads these texts (the "Mm.w.d/time" rules, <+0330> names, h:mm offsets). In the order of the names (English).
// The task: the app task reads them (a part of app_clock.c). The memory: a constant table in the app file.
typedef struct {
    const char *name[2];  // English, Hebrew
    const char *tz;
} city_t;
static const city_t CITIES[] = {
        {{"AMSTERDAM", "אמסטרדם"}, "CET-1CEST,M3.5.0,M10.5.0/3"},
        {{"ANCHORAGE", "אנקורג'"}, "AKST9AKDT,M3.2.0,M11.1.0"},
        {{"ATHENS", "אתונה"}, "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {{"AUCKLAND", "אוקלנד"}, "NZST-12NZDT,M9.5.0,M4.1.0/3"},
        {{"BANGKOK", "בנגקוק"}, "<+07>-7"},
        {{"BEIJING", "בייג'ינג"}, "CST-8"},
        {{"BERLIN", "ברלין"}, "CET-1CEST,M3.5.0,M10.5.0/3"},
        {{"BOGOTA", "בוגוטה"}, "<-05>5"},
        {{"BUENOS AIRES", "בואנוס איירס"}, "<-03>3"},
        {{"CAIRO", "קהיר"}, "EET-2EEST,M4.5.5/0,M10.5.4/24"},
        {{"CHICAGO", "שיקגו"}, "CST6CDT,M3.2.0,M11.1.0"},
        {{"DENVER", "דנוור"}, "MST7MDT,M3.2.0,M11.1.0"},
        {{"DUBAI", "דובאי"}, "<+04>-4"},
        {{"HONG KONG", "הונג קונג"}, "HKT-8"},
        {{"HONOLULU", "הונולולו"}, "HST10"},
        {{"ISTANBUL", "איסטנבול"}, "<+03>-3"},
        {{"JAKARTA", "ג'קרטה"}, "WIB-7"},
        {{"JERUSALEM", "ירושלים"}, "IST-2IDT,M3.4.4/26,M10.5.0"},
        {{"JOHANNESBURG", "יוהנסבורג"}, "SAST-2"},
        {{"KATHMANDU", "קטמנדו"}, "<+0545>-5:45"},
        {{"KYIV", "קייב"}, "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {{"LAGOS", "לאגוס"}, "WAT-1"},
        {{"LISBON", "ליסבון"}, "WET0WEST,M3.5.0/1,M10.5.0"},
        {{"LONDON", "לונדון"}, "GMT0BST,M3.5.0/1,M10.5.0"},
        {{"LOS ANGELES", "לוס אנג'לס"}, "PST8PDT,M3.2.0,M11.1.0"},
        {{"MADRID", "מדריד"}, "CET-1CEST,M3.5.0,M10.5.0/3"},
        {{"MEXICO CITY", "מקסיקו סיטי"}, "CST6"},
        {{"MOSCOW", "מוסקבה"}, "MSK-3"},
        {{"MUMBAI", "מומבאי"}, "IST-5:30"},
        {{"NAIROBI", "ניירובי"}, "EAT-3"},
        {{"NEW YORK", "ניו יורק"}, "EST5EDT,M3.2.0,M11.1.0"},
        {{"PARIS", "פריז"}, "CET-1CEST,M3.5.0,M10.5.0/3"},
        {{"PHOENIX", "פיניקס"}, "MST7"},
        {{"REYKJAVIK", "רייקיאוויק"}, "GMT0"},
        {{"ROME", "רומא"}, "CET-1CEST,M3.5.0,M10.5.0/3"},
        {{"SAN FRANCISCO", "סן פרנסיסקו"}, "PST8PDT,M3.2.0,M11.1.0"},
        {{"SAO PAULO", "סאו פאולו"}, "<-03>3"},
        {{"SEOUL", "סיאול"}, "KST-9"},
        {{"SINGAPORE", "סינגפור"}, "<+08>-8"},
        {{"SYDNEY", "סידני"}, "AEST-10AEDT,M10.1.0,M4.1.0/3"},
        {{"TEHRAN", "טהרן"}, "<+0330>-3:30"},
        {{"TEL AVIV", "תל אביב"}, "IST-2IDT,M3.4.4/26,M10.5.0"},
        {{"TOKYO", "טוקיו"}, "JST-9"},
        {{"TORONTO", "טורונטו"}, "EST5EDT,M3.2.0,M11.1.0"},
        {{"UTC", "UTC"}, "UTC0"},
        {{"VANCOUVER", "ונקובר"}, "PST8PDT,M3.2.0,M11.1.0"},
};
#define NCITY             ((int)(sizeof CITIES / sizeof CITIES[0]))
#define CITY_HOME_DEFAULT 41  // TEL AVIV
