// the music of the data pack (made by the tool gen_tab.py from pack/music.json; do not edit)
typedef struct { const char *file; uint32_t skip, len; bool loop; } tw_music_t;
enum { TWM_TITLE, TWM_SEASON0, TWM_END = TWM_SEASON0 + 4, TWM_N };
static const tw_music_t TW_MUSIC[TWM_N] = {
    {"m_title.mp3", 1105, 1832727, true},   // title
    {"m_spring.mp3", 1105, 1550769, true},   // spring
    {"m_summer.mp3", 1105, 1920000, true},   // summer
    {"m_autumn.mp3", 1105, 1591579, true},   // autumn
    {"m_winter.mp3", 1105, 1680000, true},   // winter
    {"m_end.mp3", 1105, 2176000, false},   // end
};
