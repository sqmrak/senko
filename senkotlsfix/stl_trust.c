#define _DEFAULT_SOURCE

#include "stl_gate.h"
#include "stl_log.h"
#include "stl_roots.h"
#include "stl_shadow.h"
#include "fishhook.h"

#include <Security/Security.h>
#include <string.h>

static OSStatus (*orig_SecTrustEvaluate)(SecTrustRef, SecTrustResultType *);
static OSStatus (*orig_SecTrustSetAnchorCertificates)(SecTrustRef, CFArrayRef);

#define STL_TRUST_RESULT_CONFIRM ((SecTrustResultType)2)

static int stl_trust_ok(SecTrustResultType r) {
    return r == kSecTrustResultUnspecified || r == kSecTrustResultProceed;
}

static const char *stl_trust_result_name(SecTrustResultType r) {
    switch (r) {
        case kSecTrustResultInvalid: return "invalid";
        case kSecTrustResultProceed: return "proceed";
        case STL_TRUST_RESULT_CONFIRM: return "confirm";
        case kSecTrustResultDeny: return "deny";
        case kSecTrustResultUnspecified: return "unspecified";
        case kSecTrustResultRecoverableTrustFailure: return "recoverable";
        case kSecTrustResultFatalTrustFailure: return "fatal";
        case kSecTrustResultOtherError: return "other";
        default: return "unknown";
    }
}

static int stl_should_retry_trust(OSStatus st, SecTrustResultType r) {
    if (st != errSecSuccess) return 1;
    if (stl_trust_ok(r)) return 0;
    return (r == kSecTrustResultRecoverableTrustFailure ||
            r == kSecTrustResultDeny ||
            r == kSecTrustResultFatalTrustFailure ||
            r == kSecTrustResultOtherError);
}

static OSStatus stl_SecTrustEvaluate(SecTrustRef trust, SecTrustResultType *result) {
    if (!stl_gate_is_active())
        return orig_SecTrustEvaluate(trust, result);
    if (stl_shadow_trust_is_mine(trust)) {
        CFArrayRef ours = stl_roots_anchor_array();
        if (!ours) return orig_SecTrustEvaluate(trust, result);
        SecTrustSetAnchorCertificates(trust, ours);
        return orig_SecTrustEvaluate(trust, result);
    }

    OSStatus st = orig_SecTrustEvaluate(trust, result);
    if (!stl_should_retry_trust(st, result ? *result : kSecTrustResultInvalid))
        return st;

    if (result)
        stl_log("SecTrustEvaluate system -> os=%d trust=%s",
                (int)st, stl_trust_result_name(*result));

    CFArrayRef ours = stl_roots_anchor_array();
    if (!ours) return st;

    SecTrustSetAnchorCertificates(trust, ours);
    st = orig_SecTrustEvaluate(trust, result);
    if (!stl_trust_ok(result ? *result : kSecTrustResultInvalid) || st != errSecSuccess) {
        if (result)
            stl_log("SecTrustEvaluate bundled -> os=%d trust=%s",
                    (int)st, stl_trust_result_name(*result));
    }
    return st;
}

static OSStatus stl_SecTrustSetAnchorCertificates(SecTrustRef trust, CFArrayRef anchors) {
    if (!stl_gate_is_active())
        return orig_SecTrustSetAnchorCertificates
            ? orig_SecTrustSetAnchorCertificates(trust, anchors)
            : SecTrustSetAnchorCertificates(trust, anchors);
    return orig_SecTrustSetAnchorCertificates
        ? orig_SecTrustSetAnchorCertificates(trust, anchors)
        : SecTrustSetAnchorCertificates(trust, anchors);
}

void stl_trust_install_hooks(void) {
    struct rebinding rebs[] = {
        { "SecTrustEvaluate", (void *)stl_SecTrustEvaluate, (void **)&orig_SecTrustEvaluate },
        { "SecTrustSetAnchorCertificates", (void *)stl_SecTrustSetAnchorCertificates,
          (void **)&orig_SecTrustSetAnchorCertificates },
    };
    if (rebind_symbols(rebs, sizeof(rebs) / sizeof(rebs[0])) != 0)
        stl_log("trust rebind_symbols failed");
}
