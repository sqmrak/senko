#ifndef SENKO_PLIST_BUNDLE_ID_H
#define SENKO_PLIST_BUNDLE_ID_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the CFBundleIdentifier of an app's Info.plist, read without CoreFoundation so
   the daemon can name the app behind a socket. both plist encodings occur: the
   system apps and anything built by Xcode ship a binary plist, a package that
   copies the plist by hand may leave it as xml.

   returns the identifier length (at least 1) with a terminated copy in out, or
   -1 when the file is not a plist, has no identifier, or the identifier does not
   fit. every offset in a binary plist is checked against the buffer */
int plist_bundle_id(const uint8_t *data, size_t len, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
