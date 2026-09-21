/* generated with thunk-gen v1.11 */
#define TG_ABI 3
#ifndef __CALL_v
#define __CALL_v __CALL
#endif
#ifndef __CALL_v_nr
#define __CALL_v_nr __CALL
#endif
#ifndef __CALL_P
#define __CALL_P __CALL
#endif
#ifndef __CALL_P_v
#define __CALL_P_v __CALL
#endif
#ifndef __NORET
#define __NORET
#endif

#define _THUNK0(n, r, s, f, z) \
r f(void) \
{ \
    const uint32_t _flags = z; \
    uint32_t _ret; \
    _ret = __CALL(n, NULL, 0, _flags); \
    return s(r, _ret); \
}
#define _THUNK8(n, r, s, f, t1, q1, at1, aat1, c1, l1, t2, q2, at2, aat2, c2, l2, t3, q3, at3, aat3, c3, l3, t4, q4, at4, aat4, c4, l4, t5, q5, at5, aat5, c5, l5, t6, q6, at6, aat6, c6, l6, t7, q7, at7, aat7, c7, l7, t8, q8, at8, aat8, c8, l8, z) \
r f(t1 a1 q1, t2 a2 q2, t3 a3 q3, t4 a4 q4, t5 a5 q5, t6 a6 q6, t7 a7 q7, t8 a8 q8) \
{ \
    const uint32_t _flags = z; \
    uint32_t _ret; \
    _CNV(c1, t1, at1, l1, 1); \
    _CNV(c2, t2, at2, l2, 2); \
    _CNV(c3, t3, at3, l3, 3); \
    _CNV(c4, t4, at4, l4, 4); \
    _CNV(c5, t5, at5, l5, 5); \
    _CNV(c6, t6, at6, l6, 6); \
    _CNV(c7, t7, at7, l7, 7); \
    _CNV(c8, t8, at8, l8, 8); \
    struct { \
        aat1 a1; \
        aat2 a2; \
        aat3 a3; \
        aat4 a4; \
        aat5 a5; \
        aat6 a6; \
        aat7 a7; \
        aat8 a8; \
    } PACKED _args = { _a1, _a2, _a3, _a4, _a5, _a6, _a7, _a8 }; \
    _ret = __CALL(n, (UBYTE *)&_args, sizeof(_args), _flags); \
    _UCNV(c1, l1, 1); \
    _UCNV(c2, l2, 2); \
    _UCNV(c3, l3, 3); \
    _UCNV(c4, l4, 4); \
    _UCNV(c5, l5, 5); \
    _UCNV(c6, l6, 6); \
    _UCNV(c7, l7, 7); \
    _UCNV(c8, l8, 8); \
    __CSTK(sizeof(_args)); \
    return s(r, _ret); \
}
_THUNK0(0, __ARG(DWORD), __RET, desc_probe, _TFLG_NONE)
_THUNK8(1, __ARG(DWORD), __RET, ne_enter, __ARG(DWORD), , __ARG_A(DWORD), __ARG_A(DWORD), __CNV_SIMPLE, _L_NONE, __ARG(DWORD), , __ARG_A(DWORD), __ARG_A(DWORD), __CNV_SIMPLE, _L_NONE, __ARG(DWORD), , __ARG_A(DWORD), __ARG_A(DWORD), __CNV_SIMPLE, _L_NONE, __ARG(DWORD), , __ARG_A(DWORD), __ARG_A(DWORD), __CNV_SIMPLE, _L_NONE, __ARG(DWORD), , __ARG_A(DWORD), __ARG_A(DWORD), __CNV_SIMPLE, _L_NONE, __ARG(DWORD), , __ARG_A(DWORD), __ARG_A(DWORD), __CNV_SIMPLE, _L_NONE, __ARG(DWORD), , __ARG_A(DWORD), __ARG_A(DWORD), __CNV_SIMPLE, _L_NONE, __ARG(DWORD), , __ARG_A(DWORD), __ARG_A(DWORD), __CNV_SIMPLE, _L_NONE, _TFLG_NONE)
