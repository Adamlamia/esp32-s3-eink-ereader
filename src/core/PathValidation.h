#pragma once
// ===========================================================================
//  core/PathValidation.h  —  web-input path guard (seam, review C1/S5)
// ===========================================================================
//  Shared validator for the web routes that take a book `path` query param.
//  Restricts them to a plain .txt file living directly inside BOOKS_DIR, with
//  no parent traversal and no nested subdirectories. Extracted as a pure
//  const char* function so the security contract can be unit-tested on the host
//  and reused by /api/delete, /api/bookmarks and /api/bookmark.
//
//  NOTE: this helper *is* the C1/S5 fix. Tests against it assert the post-fix
//  contract; the pre-fix code had no validation at all (any path was accepted),
//  so the reject cases below fail on pre-fix behavior and pass once the routes
//  call isBookPath().
// ===========================================================================
#include <string.h>
#include <stddef.h>

namespace core {

// True iff `path` is "<booksDir>/<name>.txt" (extension case-insensitive) with
// a non-empty <name> that contains no '/' and no "..".
inline bool isBookPath(const char *path, const char *booksDir = "/books") {
    if (!path || !booksDir) return false;

    size_t dl = strlen(booksDir);
    if (strncmp(path, booksDir, dl) != 0) return false;   // must live under booksDir
    if (path[dl] != '/') return false;                    // ...as "<booksDir>/..."

    const char *name = path + dl + 1;
    if (*name == '\0') return false;                      // empty file name

    for (const char *p = name; *p; ++p) {
        if (*p == '/') return false;                      // nested dir / trailing slash
    }
    if (strstr(name, "..") != NULL) return false;         // defensive: no parent refs

    size_t nl = strlen(name);
    if (nl < 5) return false;                             // shortest valid is "x.txt"
    const char *ext = name + nl - 4;
    return  ext[0] == '.' &&
           (ext[1] == 't' || ext[1] == 'T') &&
           (ext[2] == 'x' || ext[2] == 'X') &&
           (ext[3] == 't' || ext[3] == 'T');
}

} // namespace core
