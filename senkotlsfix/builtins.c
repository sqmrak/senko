typedef unsigned int        su_int;
typedef int                 si_int;
typedef unsigned long long  du_int;
typedef long long           di_int;

static du_int udivmod64(du_int a, du_int b, du_int *rem) {
    if (b == 0) { if (rem) *rem = 0; return 0; }   /* avoid undefined division */
    du_int q = 0, r = 0; int i;
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1ULL);
        if (r >= b) { r -= b; q |= (du_int)1 << i; }
    }
    if (rem) *rem = r;
    return q;
}
du_int __udivdi3(du_int a, du_int b)             { return udivmod64(a, b, 0); }
du_int __umoddi3(du_int a, du_int b)             { du_int r; udivmod64(a, b, &r); return r; }
du_int __udivmoddi4(du_int a, du_int b, du_int *r){ return udivmod64(a, b, r); }
di_int __divdi3(di_int a, di_int b) {
    int neg = (a < 0) ^ (b < 0);
    du_int q = udivmod64(a < 0 ? -(du_int)a : a, b < 0 ? -(du_int)b : b, 0);
    return neg ? -(di_int)q : (di_int)q;
}
di_int __moddi3(di_int a, di_int b) {
    du_int r; udivmod64(a < 0 ? -(du_int)a : a, b < 0 ? -(du_int)b : b, &r);
    return a < 0 ? -(di_int)r : (di_int)r;
}

static su_int udivmod32(su_int a, su_int b, su_int *rem) {
    if (b == 0) { if (rem) *rem = 0; return 0; }
    su_int q = 0, r = 0; int i;
    for (i = 31; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1u);
        if (r >= b) { r -= b; q |= (su_int)1 << i; }
    }
    if (rem) *rem = r;
    return q;
}
su_int __udivsi3(su_int a, su_int b) { return udivmod32(a, b, 0); }
su_int __umodsi3(su_int a, su_int b) { su_int r; udivmod32(a, b, &r); return r; }
si_int __divsi3(si_int a, si_int b) {
    int neg = (a < 0) ^ (b < 0);
    su_int q = udivmod32(a < 0 ? -(su_int)a : a, b < 0 ? -(su_int)b : b, 0);
    return neg ? -(si_int)q : (si_int)q;
}
si_int __modsi3(si_int a, si_int b) {
    su_int r; udivmod32(a < 0 ? -(su_int)a : a, b < 0 ? -(su_int)b : b, &r);
    return a < 0 ? -(si_int)r : (si_int)r;
}
