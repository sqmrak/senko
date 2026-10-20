#import <Foundation/Foundation.h>
#import <string.h>

#import "../senko_native_core.h"

#import <Security/Security.h>

__attribute__((weak))
CFArrayRef SecTrustCopyCertificateChain(SecTrustRef trust) {
    if (!trust) return NULL;
    CFIndex count = SecTrustGetCertificateCount(trust);
    CFMutableArrayRef array = CFArrayCreateMutable(kCFAllocatorDefault, count, &kCFTypeArrayCallBacks);
    for (CFIndex i = 0; i < count; i++) {
        SecCertificateRef cert = SecTrustGetCertificateAtIndex(trust, i);
        if (cert) CFArrayAppendValue(array, cert);
    }
    return array;
}

extern int NSExtensionMain(int argc, char **argv);

int main(int argc, char **argv) {
    /* senkod starts this binary as /usr/lib/senko-core on jailbroken ios 12+,
       with the same arguments xray's own cli takes */
    if (argc == 4 && strcmp(argv[1], "run") == 0 && strcmp(argv[2], "-c") == 0)
        return SenkoNativeRunFile(argv[3]);
    @autoreleasepool {
        return NSExtensionMain(argc, argv);
    }
}
