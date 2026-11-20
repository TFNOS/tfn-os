// IR DECODER: the texts in English and Hebrew (s_he). The technical names (NEC, RC5, hex, DATA) stay in English.
// The task: the app task reads them (a part of app_irdec.c). The memory: a constant table in the app file.
enum {
    T_CONNECT,
    T_CONNECTED,
    T_DEMO,
    T_NOPINS,
    T_QUIET,
    T_ALIVE,
    T_HIGH,
    T_WAIT,
    T_BACK,
    T_FAIL,
    T_SEND,
    T_SENT,
    T_SENT_DEMO,
    T_NOLED,
    T_RECEIVER,
    T_LED,
    T_RAW_TIMES,
    T_N
};
static const char *const TEXT[T_N][2] = {
        {"CONNECT THE IR MODULES", "חברו את מודולי ה-IR"},
        {"CONNECTED", "מחובר"},
        {"DEMO", "הדגמה"},
        {"THIS BOARD HAS NO PINS FOR A MODULE", "ללוח הזה אין פינים למודול"},
        {"NO IR LIGHT: PRESS A KEY OF A REMOTE",
         "אין אות. לחצו על מקש בשלט ליד המקלט"},  // (no Latin word: a colon next to it can move)
        {"IR SIGNALS ON THE PIN", "יש אותות IR על הפין"},
        {"THE PIN STAYS LOW: CHECK THE WIRES", "הפין נשאר נמוך: בדקו את החוטים"},
        {"POINT A REMOTE AT THE RECEIVER AND PRESS A KEY", "כוונו שלט אל המקלט ולחצו על מקש"},
        {"BACK", "חזרה"},
        {"THE RECEIVER FAILED", "המקלט נכשל"},
        {"SEND", "שליחה"},
        {"SENT", "נשלח"},
        {"SENT: THE DEMO RECEIVES IT", "נשלח: ההדגמה קולטת אותו"},
        {"THIS BOARD HAS NO PIN FOR AN IR LED", "ללוח הזה אין פין ללד IR"},
        {"IR RECEIVER", "מקלט IR"},
        {"IR LED", "לד IR"},
        {"THE RAW TIMES (US)", "הזמנים הגולמיים ב-US"},
};
#define TX(id) TEXT[id][s_he ? 1 : 0]
