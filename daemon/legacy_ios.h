#ifndef SENKO_LEGACY_IOS_H
#define SENKO_LEGACY_IOS_H

int senko_ios_major(void);

/* which mechanism answered: "SystemVersion.plist", "darwin kernel", or
   "unknown". the two disagree on builds that ship a binary plist, and a tester
   report that does not say which one was used cannot be reproduced */
const char *senko_ios_major_source(void);
/* parse the major version out of a SystemVersion.plist body; separated from the
   file read so the tag scan can be host tested */
int senko_ios_major_from_plist(const char *xml);

/* map a darwin kernel release onto the ios major it ships with, for the builds
   whose SystemVersion.plist is a binary property list */
int senko_ios_major_from_darwin(const char *release);

#endif
