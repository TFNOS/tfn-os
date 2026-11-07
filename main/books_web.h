#pragma once

#include "esp_http_server.h"

// The BOOKS part of the web page: /books (the page), and the API:
//   GET  /api/books                      the books (JSON), the SD card, the free space
//   POST /api/books/up?name=<file.epub>  the body is the file (checked: a ZIP with an OPF)
//   POST /api/books/del?id=<id>          delete a card or board book
//   GET  /api/books/toc?id=<id>          the metadata and the contents (JSON)
//   GET  /api/books/text?id=<id>&ch=<n>  the text of a chapter (paragraphs, plain UTF-8)
//   GET  /api/books/file?id=<id>         the EPUB file
// Uses 7 URI handlers.
void books_web_register(httpd_handle_t h);
