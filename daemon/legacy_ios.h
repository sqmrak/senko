#ifndef SENKO_LEGACY_IOS_H
#define SENKO_LEGACY_IOS_H

int senko_ios_major(void);
int senko_is_ios5(void);

/* parse the major version out of a SystemVersion.plist body; separated from the
   file read so the tag scan can be host tested */
int senko_ios_major_from_plist(const char *xml);

#endif
