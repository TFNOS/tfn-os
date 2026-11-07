#pragma once
// TFN OS: the board API of the installed apps (GET / POST /api/apps). See tfn_web.c.
void tfn_web_register(void *httpd);
// true while a web upload runs: a package (op=put of /api/apps: a put in the last 2 minutes and no commit yet) or a
// file to the SD card (op=put of /api/fs)
bool tfn_web_upload_busy(void);
