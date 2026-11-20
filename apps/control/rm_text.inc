// CONTROL: the texts in English and Hebrew (T(id): the text of the language now)
enum {
    T_APP,
    T_NEW_REMOTE,
    T_NO_COMP,
    T_SETUP_COMP,
    T_NO_LED,
    T_BUTTONS,
    T_LEARN,
    T_EDIT,
    T_DONE,
    T_EDIT_TIP,
    T_ONLINE,
    T_OFFLINE,
    T_CONNECTING,
    T_POINT,
    T_NEXT_KEY,
    T_GOT_RAW,
    T_NAME_IT,
    T_TEST,
    T_OTHER_NAME,
    T_AGAIN,
    T_SENT,
    T_NO_ANSWER,
    T_RENAME,
    T_DELETE,
    T_DELETE_REM_Q,
    T_DELETE_BTN_Q,
    T_LEARN_AGAIN,
    T_MOVE_BACK,
    T_MOVE_ON,
    T_CANCEL,
    T_NAME,
    T_SAVED,
    T_FULL,
    T_TIMEOUT,
    T_NO_RX,
    T_COMPANIONS,
    T_REMOTE,
    T_KEY,
    T_TIMES,
    T_ADDRESS,
    T_COMMAND,
    T_RELEARNED,
    T_SAVE,
    T_DUP,
    T_ADD,
    T_ADD_IR,
    T_ADD_SWITCH,
    T_ADD_SENSOR,
    T_PICK_SWITCH,
    T_PICK_SENSOR,
    T_NO_MODULE,
    T_WHAT_DO,
    T_TOGGLE,
    T_ON,
    T_OFF,
    T_PRESS,
    T_ADD_RF,
    T_NO_RF_RX,
    T_RF_POINT,
    T_NO_RF_TX,
    T_N
};
static const char *const TXT[T_N][2] = {
        {"CONTROL", "שליטה"},
        {"NEW REMOTE", "שלט חדש"},
        {"A remote here works through a TFN companion: a small ESP32 with modules (IR, relays, buttons, sensors).",
         "שלט כאן עובד דרך מלווה: לוח ESP32 קטן עם רכיבים כמו IR, ממסרים, לחצנים וחיישנים."},
        {"SET UP A COMPANION", "הגדרת מלווה"},
        {"Your companions have no modules yet. Add the modules on their pins in COMPANIONS.",
         "למלווים שלכם אין עדיין רכיבים. הוסיפו את הרכיבים שעל הפינים במסך המלווים."},
        {"KEYS", "מקשים"},
        {"LEARN", "למידה"},
        {"EDIT", "עריכה"},
        {"DONE", "סיום"},
        {"Tap a key to rename, move, learn again or delete it.",
         "לחצו על מקש כדי לשנות שם, להזיז, ללמוד שוב או למחוק."},
        {"ONLINE", "מחובר"},
        {"OFFLINE", "לא מחובר"},
        {"Connecting to the companion...", "מתחבר למלווה..."},
        {"Point your remote at the companion and press one key.", "כוונו את השלט אל המלווה ולחצו על מקש אחד."},
        {"Saved. Press the next key of your remote.", "נשמר. לחצו על המקש הבא בשלט."},
        {"UNKNOWN CODE: AN EXACT COPY", "קוד לא מוכר: העתק מדויק"},
        {"Tap a name to save the key:", "לחצו על שם כדי לשמור את המקש:"},
        {"TEST", "בדיקה"},
        {"OTHER NAME", "שם אחר"},
        {"AGAIN", "שוב"},
        {"SENT", "נשלח"},
        {"NO ANSWER", "אין תשובה"},
        {"RENAME", "שם חדש"},
        {"DELETE", "מחיקה"},
        {"Delete this remote and all its keys?", "למחוק את השלט ואת כל המקשים שלו?"},
        {"Delete this key?", "למחוק את המקש?"},
        {"LEARN AGAIN", "למידה מחדש"},
        {"MOVE BACK", "הזזה אחורה"},
        {"MOVE ON", "הזזה קדימה"},
        {"CANCEL", "ביטול"},
        {"NAME", "שם"},
        {"SAVED", "נשמר"},
        {"NO ROOM", "אין מקום"},
        {"No key came in time.", "לא הגיע מקש בזמן."},
        {"No companion has an IR receiver. Add one in COMPANIONS.", "לאף מלווה אין מקלט IR. הוסיפו אחד במסך המלווים."},
        {"COMPANIONS", "מלווים"},
        {"REMOTE", "שלט"},
        {"KEY", "מקש"},
        {"TIMES", "זמנים"},
        {"ADDRESS", "כתובת"},
        {"COMMAND", "פקודה"},
        {"LEARNED AGAIN", "נלמד מחדש"},
        {"SAVE", "שמירה"},
        {"The same code as \"%s\": the right key? Else AGAIN.", "אותו קוד כמו \"%s\": המקש הנכון? אחרת שוב."},
        {"ADD A KEY", "הוספת מקש"},
        {"IR KEY", "מקש IR"},
        {"SWITCH", "מתג"},
        {"SENSOR", "חיישן"},
        {"WHICH SWITCH?", "איזה מתג?"},
        {"WHICH SENSOR?", "איזה חיישן?"},
        {"No companion has such a module. Add it on a pin in COMPANIONS.",
         "לאף מלווה אין רכיב כזה. הוסיפו אותו על פין במסך המלווים."},
        {"WHAT DOES THE KEY DO?", "מה המקש עושה?"},
        {"ON / OFF", "הפעלה / כיבוי"},
        {"ON", "פועל"},
        {"OFF", "כבוי"},
        {"PRESS 0.5 s", "לחיצה 0.5 שנ'"},
        {"RF KEY (433 / 315)", "מקש RF של 433 / 315"},
        {"No companion has a 433 or 315 MHz receiver. Add one in COMPANIONS.",
         "לאף מלווה אין מקלט 433 או 315 מגה-הרץ. הוסיפו אחד במסך המלווים."},
        {"Hold the key of your remote near the companion.", "החזיקו את המקש של השלט ליד המלווה."},
        {"No companion has a transmitter of this band: the key cannot send yet.",
         "לאף מלווה אין משדר בתדר הזה: המקש לא יכול לשלוח עדיין."},
};
static const char *T(int id)
{
    return TXT[id][s_he ? 1 : 0];
}

// the names of the roles of the switch and sensor modules (the default names of their keys)
static const char *role_word(int role)
{
    static const char *const W[][2] = {{"RELAY", "ממסר"},   {"LIGHT", "אור"}, {"BUTTON", "לחצן"},
                                       {"MOTION", "תנועה"}, {"DOOR", "דלת"},  {"SENSOR", "חיישן"}};
    const int k = role - TC_R_RELAY;
    return k >= 0 && k < 6 ? W[k][s_he ? 1 : 0] : tc_role_name(role);
}

// the word of the state of a switch module ("ON", "PRESSED", "MOTION", "CLOSED" ...)
static const char *state_word(int role, int v)
{
    static const char *const W[][4] = {{"PRESSED", "UP", "לחוץ", "משוחרר"},
                                       {"MOTION", "STILL", "תנועה", "שקט"},
                                       {"CLOSED", "OPEN", "סגור", "פתוח"}};
    const int k = role == TC_R_BUTTON ? 0 : role == TC_R_MOTION ? 1 : role == TC_R_CONTACT ? 2 : -1;
    if (k < 0) {
        return T(v ? T_ON : T_OFF);
    }
    return W[k][(v ? 0 : 1) + (s_he ? 2 : 0)];
}

// the names that LEARN offers (a tap saves the key with that name; a name that the remote has already is left out)
static const char *const NAMES[][2] = {
        {"POWER", "הפעלה"}, {"VOL +", "עוצמה +"}, {"VOL -", "עוצמה -"}, {"MUTE", "השתקה"},   {"CH +", "ערוץ +"},
        {"CH -", "ערוץ -"}, {"OK", "אישור"},      {"MENU", "תפריט"},    {"UP", "למעלה"},     {"DOWN", "למטה"},
        {"LEFT", "שמאלה"},  {"RIGHT", "ימינה"},   {"BACK", "חזרה"},     {"SET", "הגדרה"},    {"+", "+"},
        {"-", "-"},         {"HOME", "בית"},      {"SOURCE", "מקור"},   {"PLAY", "הפעל"},    {"STOP", "עצור"},
        {"OPEN", "פתיחה"},  {"CLOSE", "סגירה"},   {"LOCK", "נעילה"},    {"UNLOCK", "שחרור"}, {"GATE", "שער"},
        {"LIGHT", "אור"},
};
#define NNAMES ((int)(sizeof NAMES / sizeof NAMES[0]))
