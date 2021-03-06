// mm_vad.c : implementation of Windows VAD (virtual address descriptor) functionality.
//
// (c) Ulf Frisk, 2019-2020
// Author: Ulf Frisk, pcileech@frizk.net
//
#include "vmm.h"
#include "mm.h"
#include "vmmwindef.h"

#define MMVAD_POOLTAG_VAD       'Vad '
#define MMVAD_POOLTAG_VADF      'VadF'
#define MMVAD_POOLTAG_VADS      'VadS'
#define MMVAD_POOLTAG_VADL      'Vadl'
#define MMVAD_POOLTAG_VADM      'Vadm'

#define MMVAD_PTESIZE           ((ctxVmm->tpMemoryModel == VMM_MEMORYMODEL_X86) ? 4 : 8)

// ----------------------------------------------------------------------------
// DEFINES OF VAD STRUCTS FOR DIFFRENT WINDOWS VERSIONS
// Define the VADs here statically rather than parse offsets it from PDBs
// to keep the VAD functionality fast and independent of PDBs.
// ----------------------------------------------------------------------------

typedef enum _tdMMVAD_TYPE {
    VadNone                 = 0,
    VadDevicePhysicalMemory = 1,
    VadImageMap             = 2,
    VadAwe                  = 3,
    VadWriteWatch           = 4,
    VadLargePages           = 5,
    VadRotatePhysical       = 6,
    VadLargePageSection     = 7
} _MMVAD_TYPE;

// WinXP 32-bit
typedef struct _tdMMVAD32_XP {
    DWORD _Dummy1;
    DWORD PoolTag;
    // _MMVAD_SHORT
    DWORD StartingVpn;
    DWORD EndingVpn;
    DWORD Parent;
    DWORD LeftChild;
    DWORD RightChild;
    union {
        struct {
            DWORD CommitCharge      : 19;   // Pos 0
            DWORD PhysicalMapping   : 1;    // Pos 19
            DWORD ImageMap          : 1;    // Pos 20
            DWORD UserPhysicalPages : 1;    // Pos 21
            DWORD NoChange          : 1;    // Pos 22
            DWORD WriteWatch        : 1;    // Pos 23
            DWORD Protection        : 5;    // Pos 24
            DWORD LargePages        : 1;    // Pos 29
            DWORD MemCommit         : 1;    // Pos 30
            DWORD PrivateMemory     : 1;    // Pos 31
        };
        DWORD u;
    };
    // _MMVAD
    DWORD ControlArea;
    DWORD FirstPrototypePte;
    DWORD LastContiguousPte;
    union {
        struct {
            DWORD FileOffset        : 24;   // Pos 0
            DWORD SecNoChange       : 1;    // Pos 24
            DWORD OneSecured        : 1;    // Pos 25
            DWORD MultipleSecured   : 1;    // Pos 26
            DWORD ReadOnly          : 1;    // Pos 27
            DWORD LongVad           : 1;    // Pos 28
            DWORD ExtendableFile    : 1;    // Pos 29
            DWORD Inherit           : 1;    // Pos 30
            DWORD CopyOnWrite       : 1;    // Pos 31
        };
        DWORD u2;
    };
} _MMVAD32_XP;

// Vista/7 32-bit
typedef struct _tdMMVAD32_7 {
    DWORD _Dummy1;
    DWORD PoolTag;
    // _MMVAD_SHORT
    DWORD u1;
    DWORD LeftChild;
    DWORD RightChild;
    DWORD StartingVpn;
    DWORD EndingVpn;
    union {
        struct {
            DWORD CommitCharge      : 19;   // Pos 0
            DWORD NoChange          : 1;    // Pos 51
            DWORD VadType           : 3;    // Pos 52
            DWORD MemCommit         : 1;    // Pos 55
            DWORD Protection        : 5;    // Pos 56
            DWORD _Spare1           : 2;    // Pos 61
            DWORD PrivateMemory     : 1;    // Pos 63
        };
        DWORD u;
    };
    DWORD PushLock;
    DWORD u5;
    // _MMVAD
    union {
        struct {
            DWORD FileOffset        : 24;   // Pos 0
            DWORD SecNoChange       : 1;    // Pos 24
            DWORD OneSecured        : 1;    // Pos 25
            DWORD MultipleSecured   : 1;    // Pos 26
            DWORD _Spare2           : 1;    // Pos 27
            DWORD LongVad           : 1;    // Pos 28
            DWORD ExtendableFile    : 1;    // Pos 29
            DWORD Inherit           : 1;    // Pos 30
            DWORD CopyOnWrite       : 1;    // Pos 31
        };
        DWORD u2;
    };
    DWORD Subsection;
    DWORD FirstPrototypePte;
    DWORD LastContiguousPte;
    DWORD ViewLinks[2];
    DWORD VadsProcess;
} _MMVAD32_7;

// Vista/7 64-bit
typedef struct _tdMMVAD64_7 {
    DWORD _Dummy1;
    DWORD PoolTag;
    QWORD _Dummy2;
    // _MMVAD_SHORT
    QWORD u1;
    QWORD LeftChild;
    QWORD RightChild;
    QWORD StartingVpn;
    QWORD EndingVpn;
    union {
        struct {
            QWORD CommitCharge      : 51;   // Pos 0
            QWORD NoChange          : 1;    // Pos 51
            QWORD VadType           : 3;    // Pos 52
            QWORD MemCommit         : 1;    // Pos 55
            QWORD Protection        : 5;    // Pos 56
            QWORD _Spare1           : 2;    // Pos 61
            QWORD PrivateMemory     : 1;    // Pos 63
        };
        QWORD u;
    };
    QWORD PushLock;
    QWORD u5;
    // _MMVAD
    union {
        struct {
            DWORD FileOffset        : 24;   // Pos 0
            DWORD SecNoChange       : 1;    // Pos 24
            DWORD OneSecured        : 1;    // Pos 25
            DWORD MultipleSecured   : 1;    // Pos 26
            DWORD _Spare2           : 1;    // Pos 27
            DWORD LongVad           : 1;    // Pos 28
            DWORD ExtendableFile    : 1;    // Pos 29
            DWORD Inherit           : 1;    // Pos 30
            DWORD CopyOnWrite       : 1;    // Pos 31
        };
        QWORD u2;
    };
    QWORD Subsection;
    QWORD FirstPrototypePte;
    QWORD LastContiguousPte;
    QWORD ViewLinks[2];
    QWORD VadsProcess;
} _MMVAD64_7;

// Win8.0 32-bit
typedef struct _tdMMVAD32_80 {
    DWORD PoolTag;
    QWORD _Dummy2;
    // _MMVAD_SHORT
    DWORD __u1;
    DWORD LeftChild;
    DWORD RightChild;
    DWORD StartingVpn;
    DWORD EndingVpn;
    DWORD PushLock;
    union {
        struct {
            DWORD VadType           : 3;    // Pos 0
            DWORD Protection        : 5;    // Pos 3
            DWORD PreferredNode     : 6;    // Pos 8
            DWORD NoChange          : 1;    // Pos 14
            DWORD PrivateMemory     : 1;    // Pos 15
            DWORD Teb               : 1;    // Pos 16
            DWORD PrivateFixup      : 1;    // Pos 17
            DWORD _Spare1           : 13;   // Pos 18
            DWORD DeleteInProgress  : 1;    // Pos 31
        };
        DWORD u;
    };
    union {
        struct {
            DWORD CommitCharge      : 31;   // Pos 0
            DWORD MemCommit         : 1;    // Pos 31
        };
        DWORD u1;
    };
    DWORD EventList;
    DWORD ReferenceCount;
    // _MMVAD
    union {
        struct {
            DWORD FileOffset        : 24;   // Pos 0
            DWORD Large             : 1;    // Pos 24
            DWORD TrimBehind        : 1;    // Pos 25
            DWORD Inherit           : 1;    // Pos 26
            DWORD CopyOnWrite       : 1;    // Pos 27
            DWORD NoValidationNeeded : 1;   // Pos 28
            DWORD _Spare2           : 3;    // Pos 29
        };
        DWORD u2;
    };
    DWORD Subsection;
    DWORD FirstPrototypePte;
    DWORD LastContiguousPte;
    DWORD ViewLinks[2];
    DWORD VadsProcess;
    DWORD u4;
} _MMVAD32_80;

// Win8.0 64-bit
typedef struct _tdMMVAD64_80 {
    DWORD _Dummy1;
    DWORD PoolTag;
    QWORD _Dummy2;
    // _MMVAD_SHORT
    QWORD __u1;
    QWORD LeftChild;
    QWORD RightChild;
    DWORD StartingVpn;
    DWORD EndingVpn;
    QWORD PushLock;
    union {
        struct {
            DWORD VadType           : 3;    // Pos 0
            DWORD Protection        : 5;    // Pos 3
            DWORD PreferredNode     : 6;    // Pos 8
            DWORD NoChange          : 1;    // Pos 14
            DWORD PrivateMemory     : 1;    // Pos 15
            DWORD Teb               : 1;    // Pos 16
            DWORD PrivateFixup      : 1;    // Pos 17
            DWORD _Spare1           : 13;   // Pos 18
            DWORD DeleteInProgress  : 1;    // Pos 31
        };
        DWORD u;
    };
    union {
        struct {
            DWORD CommitCharge      : 31;   // Pos 0
            DWORD MemCommit         : 1;    // Pos 31
        };
        DWORD u1;
    };
    QWORD EventList;
    DWORD ReferenceCount;
    DWORD _Filler;
    // _MMVAD
    union {
        struct {
            DWORD FileOffset        : 24;   // Pos 0
            DWORD Large             : 1;    // Pos 24
            DWORD TrimBehind        : 1;    // Pos 25
            DWORD Inherit           : 1;    // Pos 26
            DWORD CopyOnWrite       : 1;    // Pos 27
            DWORD NoValidationNeeded : 1;   // Pos 28
            DWORD _Spare2           : 3;    // Pos 29
        };
        QWORD u2;
    };
    QWORD Subsection;
    QWORD FirstPrototypePte;
    QWORD LastContiguousPte;
    QWORD ViewLinks[2];
    QWORD VadsProcess;
    QWORD u4;
} _MMVAD64_80;

// Win8.1/10 32-bit
typedef struct _tdMMVAD32_10 {
    DWORD _Dummy1;
    DWORD PoolTag;
    // _MMVAD_SHORT
    DWORD Children[2];
    DWORD ParentValue;
    DWORD StartingVpn;
    DWORD EndingVpn;
    DWORD ReferenceCount;
    DWORD PushLock;
    DWORD u;    // no struct - bit order varies too much in Win10
    union {
        struct {
            DWORD CommitCharge      : 31;   // Pos 0
            DWORD MemCommit         : 1;    // Pos 31
        };
        DWORD u1;
    };
    DWORD EventList;
    // _MMVAD
    union {
        struct {
            DWORD FileOffset        : 24;   // Pos 0
            DWORD Large             : 1;    // Pos 24
            DWORD TrimBehind        : 1;    // Pos 25
            DWORD Inherit           : 1;    // Pos 26
            DWORD CopyOnWrite       : 1;    // Pos 27
            DWORD NoValidationNeeded : 1;   // Pos 28
            DWORD _Spare2           : 3;    // Pos 29
        };
        DWORD u2;
    };
    DWORD Subsection;
    DWORD FirstPrototypePte;
    DWORD LastContiguousPte;
    DWORD ViewLinks[2];
    DWORD VadsProcess;
    DWORD u4;
    DWORD FileObject;
} _MMVAD32_10;

// Win8.1/10 64-bit
typedef struct _tdMMVAD64_10 {
    DWORD _Dummy1;
    DWORD PoolTag;
    QWORD _Dummy2;
    // _MMVAD_SHORT
    QWORD Children[2];
    QWORD ParentValue;
    DWORD StartingVpn;
    DWORD EndingVpn;
    BYTE StartingVpnHigh;
    BYTE EndingVpnHigh;
    BYTE CommitChargeHigh;
    BYTE SpareNT64VadUChar;
    QWORD PushLock;
    DWORD u;    // no struct - bit order varies too much in Win10
    union {
        struct {
            DWORD CommitCharge      : 31;   // Pos 0
            DWORD MemCommit         : 1;    // Pos 31
        };
        DWORD u1;
    };
    QWORD EventList;
    // _MMVAD
    union {
        struct {
            DWORD FileOffset        : 24;   // Pos 0
            DWORD Large             : 1;    // Pos 24
            DWORD TrimBehind        : 1;    // Pos 25
            DWORD Inherit           : 1;    // Pos 26
            DWORD CopyOnWrite       : 1;    // Pos 27
            DWORD NoValidationNeeded : 1;   // Pos 28
            DWORD _Spare2           : 3;    // Pos 29
        };
        QWORD u2;
    };
    QWORD Subsection;
    QWORD FirstPrototypePte;
    QWORD LastContiguousPte;
    QWORD ViewLinks[2];
    QWORD VadsProcess;
    QWORD u4;
    QWORD FileObject;
} _MMVAD64_10;

/*
* Object manager callback function for object cleanup tasks.
*/
VOID VmmVad_MemMapVad_CloseObCallback(_In_ PVOID pVmmOb)
{
    PVMMOB_MAP_VAD pOb = (PVMMOB_MAP_VAD)pVmmOb;
    LocalFree(pOb->wszMultiText);
}

// ----------------------------------------------------------------------------
// IMPLEMENTATION OF VAD PARSING FUNCTIONALITY FOR DIFFERENT WINDOWS VERSIONS:
// ----------------------------------------------------------------------------

/*
* Comparator / Sorting function for qsort of VMM_VADMAP_ENTRIES.
* -- v1
* -- v2
* -- return
*/
int MmVad_CmpVadEntry(const void *v1, const void *v2)
{
    return
        (*(PQWORD)v1 < *(PQWORD)v2) ? -1 :
        (*(PQWORD)v1 > * (PQWORD)v2) ? 1 : 0;
}

BOOL MmVad_Spider_PoolTagAny(_In_ DWORD dwPoolTag, _In_ DWORD cPoolTag, ...)
{
    va_list argp;
    dwPoolTag = _byteswap_ulong(dwPoolTag);
    va_start(argp, cPoolTag);
    while(cPoolTag) {
        if(dwPoolTag == va_arg(argp, DWORD)) {
            va_end(argp);
            return TRUE;
        }
        cPoolTag--;
    }
    va_end(argp);
    return FALSE;
}

PVMM_MAP_VADENTRY MmVad_Spider_MMVAD32_XP(_In_ PVMM_PROCESS pSystemProcess, _In_ QWORD va, _In_ PVMMOB_MAP_VAD pmVad, _In_ POB_SET psAll, _In_ POB_SET psTry1, _In_opt_ POB_SET psTry2, _In_ QWORD fVmmRead, _In_ DWORD dwReserved)
{
    _MMVAD32_XP v = { 0 };
    PVMM_MAP_VADENTRY e;
    if(!VmmRead2(pSystemProcess, va, (PBYTE)&v, sizeof(_MMVAD32_XP), fVmmRead | VMM_FLAG_FORCECACHE_READ)) {
        ObSet_Push(psTry2, va);
        return NULL;
    }
    if((v.EndingVpn < v.StartingVpn) || !MmVad_Spider_PoolTagAny(v.PoolTag, 5, MMVAD_POOLTAG_VADS, MMVAD_POOLTAG_VAD, MMVAD_POOLTAG_VADL, MMVAD_POOLTAG_VADM, MMVAD_POOLTAG_VADF)) {
        return NULL;
    }
    // short vad
    e = &pmVad->pMap[pmVad->cMap++];
    if(VMM_KADDR32_8(v.LeftChild)) {
        ObSet_Push(psAll, v.LeftChild - 8);
        ObSet_Push(psTry1, v.LeftChild - 8);
    }
    if(VMM_KADDR32_8(v.RightChild)) {
        ObSet_Push(psAll, v.RightChild - 8);
        ObSet_Push(psTry1, v.RightChild - 8);
    }
    e->vaStart = (QWORD)v.StartingVpn << 12;
    e->vaEnd = ((QWORD)v.EndingVpn << 12) | 0xfff;
    e->CommitCharge = v.CommitCharge;
    e->MemCommit = v.MemCommit;
    e->VadType = 0;
    e->Protection = v.Protection;
    e->fPrivateMemory = v.PrivateMemory;
    if(VMM_POOLTAG(v.PoolTag, MMVAD_POOLTAG_VADL)) { e->VadType = VadLargePages; }
    // full vad
    if(v.PoolTag == MMVAD_POOLTAG_VADS) { return e; }
    e->vaSubsection = v.ControlArea;
    if(VMM_KADDR32_4(v.FirstPrototypePte)) {
        e->vaPrototypePte = v.FirstPrototypePte;
        e->cbPrototypePte = (DWORD)(v.LastContiguousPte - v.FirstPrototypePte + MMVAD_PTESIZE);
    }
    return e;
}

PVMM_MAP_VADENTRY MmVad_Spider_MMVAD32_7(_In_ PVMM_PROCESS pSystemProcess, _In_ QWORD va, _In_ PVMMOB_MAP_VAD pmVad, _In_ POB_SET psAll, _In_ POB_SET psTry1, _In_opt_ POB_SET psTry2, _In_ QWORD fVmmRead, _In_ DWORD dwReserved)
{
    _MMVAD32_7 v = { 0 };
    PVMM_MAP_VADENTRY e;
    if(!VmmRead2(pSystemProcess, va, (PBYTE)&v, sizeof(_MMVAD32_7), fVmmRead | VMM_FLAG_FORCECACHE_READ)) {
        ObSet_Push(psTry2, va);
        return NULL;
    }
    if((v.EndingVpn < v.StartingVpn) || !MmVad_Spider_PoolTagAny(v.PoolTag, 5, MMVAD_POOLTAG_VADS, MMVAD_POOLTAG_VAD, MMVAD_POOLTAG_VADL, MMVAD_POOLTAG_VADM, MMVAD_POOLTAG_VADF)) {
        return NULL;
    }
    // short vad
    e = &pmVad->pMap[pmVad->cMap++];
    if(VMM_KADDR32_8(v.LeftChild)) {
        ObSet_Push(psAll, v.LeftChild - 8);
        ObSet_Push(psTry1, v.LeftChild - 8);
    }
    if(VMM_KADDR32_8(v.RightChild)) {
        ObSet_Push(psAll, v.RightChild - 8);
        ObSet_Push(psTry1, v.RightChild - 8);
    }
    e->vaStart = (QWORD)v.StartingVpn << 12;
    e->vaEnd = ((QWORD)v.EndingVpn << 12) | 0xfff;
    e->CommitCharge = v.CommitCharge;
    e->MemCommit = v.MemCommit;
    e->VadType = v.VadType;
    e->Protection = v.Protection;
    e->fPrivateMemory = v.PrivateMemory;
    // full vad
    if(v.PoolTag == MMVAD_POOLTAG_VADS) { return e; }
    e->vaSubsection = v.Subsection;
    if(VMM_KADDR32_4(v.FirstPrototypePte)) {
        e->vaPrototypePte = v.FirstPrototypePte;
        e->cbPrototypePte = (DWORD)(v.LastContiguousPte - v.FirstPrototypePte + MMVAD_PTESIZE);
    }
    return e;
}

PVMM_MAP_VADENTRY MmVad_Spider_MMVAD64_7(_In_ PVMM_PROCESS pSystemProcess, _In_ QWORD va, _In_ PVMMOB_MAP_VAD pmVad, _In_ POB_SET psAll, _In_ POB_SET psTry1, _In_opt_ POB_SET psTry2, _In_ QWORD fVmmRead, _In_ DWORD dwReserved)
{
    _MMVAD64_7 v = { 0 };
    PVMM_MAP_VADENTRY e;
    if(!VmmRead2(pSystemProcess, va, (PBYTE)&v, sizeof(_MMVAD64_7), fVmmRead | VMM_FLAG_FORCECACHE_READ)) {
        ObSet_Push(psTry2, va);
        return NULL;
    }
    if((v.EndingVpn < v.StartingVpn) || !MmVad_Spider_PoolTagAny(v.PoolTag, 5, MMVAD_POOLTAG_VADS, MMVAD_POOLTAG_VAD, MMVAD_POOLTAG_VADL, MMVAD_POOLTAG_VADM, MMVAD_POOLTAG_VADF)) {
        return NULL;
    }
    // short vad
    e = &pmVad->pMap[pmVad->cMap++];
    if(VMM_KADDR64_16(v.LeftChild)) {
        ObSet_Push(psAll, v.LeftChild - 0x10);
        ObSet_Push(psTry1, v.LeftChild - 0x10);
    }
    if(VMM_KADDR64_16(v.RightChild)) {
        ObSet_Push(psAll, v.RightChild - 0x10);
        ObSet_Push(psTry1, v.RightChild - 0x10);
    }
    e->vaStart = (QWORD)v.StartingVpn << 12;
    e->vaEnd = ((QWORD)v.EndingVpn << 12) | 0xfff;
    e->CommitCharge = (DWORD)v.CommitCharge;
    e->MemCommit = (DWORD)v.MemCommit;
    e->VadType = (DWORD)v.VadType;
    e->Protection = (DWORD)v.Protection;
    e->fPrivateMemory = (DWORD)v.PrivateMemory;
    // full vad
    if(v.PoolTag == MMVAD_POOLTAG_VADS) { return e; }
    e->vaSubsection = v.Subsection;
    if(VMM_KADDR64_8(v.FirstPrototypePte)) {
        e->vaPrototypePte = v.FirstPrototypePte;
        e->cbPrototypePte = (DWORD)(v.LastContiguousPte - v.FirstPrototypePte + 8);
    }
    return e;
}

PVMM_MAP_VADENTRY MmVad_Spider_MMVAD32_80(_In_ PVMM_PROCESS pSystemProcess, _In_ QWORD va, _In_ PVMMOB_MAP_VAD pmVad, _In_ POB_SET psAll, _In_ POB_SET psTry1, _In_opt_ POB_SET psTry2, _In_ QWORD fVmmRead, _In_ DWORD dwReserved)
{
    _MMVAD32_80 v = { 0 };
    PVMM_MAP_VADENTRY e;
    if(!VmmRead2(pSystemProcess, va, (PBYTE)&v, sizeof(_MMVAD32_80), fVmmRead | VMM_FLAG_FORCECACHE_READ)) {
        ObSet_Push(psTry2, va);
        return NULL;
    }
    if((v.EndingVpn < v.StartingVpn) || !MmVad_Spider_PoolTagAny(v.PoolTag, 5, MMVAD_POOLTAG_VADS, MMVAD_POOLTAG_VAD, MMVAD_POOLTAG_VADL, MMVAD_POOLTAG_VADM, MMVAD_POOLTAG_VADF)) {
        return NULL;
    }
    // short vad
    e = &pmVad->pMap[pmVad->cMap++];
    if(VMM_KADDR64_16(v.LeftChild)) {
        ObSet_Push(psAll, v.LeftChild - 8);
        ObSet_Push(psTry1, v.LeftChild - 8);
    }
    if(VMM_KADDR64_16(v.RightChild)) {
        ObSet_Push(psAll, v.RightChild - 8);
        ObSet_Push(psTry1, v.RightChild - 8);
    }
    e->vaStart = (QWORD)v.StartingVpn << 12;
    e->vaEnd = ((QWORD)v.EndingVpn << 12) | 0xfff;
    e->CommitCharge = (DWORD)v.CommitCharge;
    e->MemCommit = (DWORD)v.MemCommit;
    e->VadType = (DWORD)v.VadType;
    e->Protection = (DWORD)v.Protection;
    e->fPrivateMemory = (DWORD)v.PrivateMemory;
    // full vad
    if(v.PoolTag == MMVAD_POOLTAG_VADS) { return e; }
    e->flags[2] = v.u2;
    e->vaSubsection = v.Subsection;
    if(VMM_KADDR32_8(v.FirstPrototypePte)) {
        e->vaPrototypePte = v.FirstPrototypePte;
        e->cbPrototypePte = (DWORD)(v.LastContiguousPte - v.FirstPrototypePte + 8);
    }
    return e;
}

PVMM_MAP_VADENTRY MmVad_Spider_MMVAD64_80(_In_ PVMM_PROCESS pSystemProcess, _In_ QWORD va, _In_ PVMMOB_MAP_VAD pmVad, _In_ POB_SET psAll, _In_ POB_SET psTry1, _In_opt_ POB_SET psTry2, _In_ QWORD fVmmRead, _In_ DWORD dwReserved)
{
    _MMVAD64_80 v = { 0 };
    PVMM_MAP_VADENTRY e;
    if(!VmmRead2(pSystemProcess, va, (PBYTE)&v, sizeof(_MMVAD64_80), fVmmRead | VMM_FLAG_FORCECACHE_READ)) {
        ObSet_Push(psTry2, va);
        return NULL;
    }
    if((v.EndingVpn < v.StartingVpn) || !MmVad_Spider_PoolTagAny(v.PoolTag, 5, MMVAD_POOLTAG_VADS, MMVAD_POOLTAG_VAD, MMVAD_POOLTAG_VADL, MMVAD_POOLTAG_VADM, MMVAD_POOLTAG_VADF)) {
        return NULL;
    }
    // short vad
    e = &pmVad->pMap[pmVad->cMap++];
    if(VMM_KADDR64_16(v.LeftChild)) {
        ObSet_Push(psAll, v.LeftChild - 0x10);
        ObSet_Push(psTry1, v.LeftChild - 0x10);
    }
    if(VMM_KADDR64_16(v.RightChild)) {
        ObSet_Push(psAll, v.RightChild - 0x10);
        ObSet_Push(psTry1, v.RightChild - 0x10);
    }
    e->vaStart = (QWORD)v.StartingVpn << 12;
    e->vaEnd = ((QWORD)v.EndingVpn << 12) | 0xfff;
    e->CommitCharge = v.CommitCharge;
    e->MemCommit = v.MemCommit;
    e->VadType = v.VadType;
    e->Protection = v.Protection;
    e->fPrivateMemory = v.PrivateMemory;
    // full vad
    if(v.PoolTag == MMVAD_POOLTAG_VADS) { return e; }
    e->flags[2] = (DWORD)v.u2;
    e->vaSubsection = v.Subsection;
    if(VMM_KADDR64_8(v.FirstPrototypePte)) {
        e->vaPrototypePte = v.FirstPrototypePte;
        e->cbPrototypePte = (DWORD)(v.LastContiguousPte - v.FirstPrototypePte);
    }
    return e;
}

PVMM_MAP_VADENTRY MmVad_Spider_MMVAD32_10(_In_ PVMM_PROCESS pSystemProcess, _In_ QWORD va, _In_ PVMMOB_MAP_VAD pmVad, _In_ POB_SET psAll, _In_ POB_SET psTry1, _In_opt_ POB_SET psTry2, _In_ QWORD fVmmRead, _In_ DWORD dwFlagsBitMask)
{
    _MMVAD32_10 v = { 0 };
    PVMM_MAP_VADENTRY e;
    if(!VmmRead2(pSystemProcess, va, (PBYTE)&v, sizeof(_MMVAD32_10), fVmmRead | VMM_FLAG_FORCECACHE_READ)) {
        ObSet_Push(psTry2, va);
        return NULL;
    }
    if((v.EndingVpn < v.StartingVpn) || !MmVad_Spider_PoolTagAny(v.PoolTag, 5, MMVAD_POOLTAG_VADS, MMVAD_POOLTAG_VAD, MMVAD_POOLTAG_VADL, MMVAD_POOLTAG_VADM, MMVAD_POOLTAG_VADF)) {
        return NULL;
    }
    // short vad
    e = &pmVad->pMap[pmVad->cMap++];
    if(VMM_KADDR32_8(v.Children[0])) {
        ObSet_Push(psAll, v.Children[0] - 8);
        ObSet_Push(psTry1, v.Children[0] - 8);
    }
    if(VMM_KADDR32_8(v.Children[1])) {
        ObSet_Push(psAll, v.Children[1] - 8);
        ObSet_Push(psTry1, v.Children[1] - 8);
    }
    e->vaStart = (QWORD)v.StartingVpn << 12;
    e->vaEnd = ((QWORD)v.EndingVpn << 12) | 0xfff;
    e->CommitCharge = v.CommitCharge;
    e->MemCommit = v.MemCommit;
    e->VadType = 0x07 & (v.u >> (dwFlagsBitMask & 0xff));
    e->Protection = 0x1f & (v.u >> ((dwFlagsBitMask >> 8) & 0xff));
    e->fPrivateMemory = 0x01 & (v.u >> ((dwFlagsBitMask >> 16) & 0xff));
    // full vad
    if(v.PoolTag == MMVAD_POOLTAG_VADS) { return e; }
    e->flags[2] = v.u2;
    e->vaSubsection = v.Subsection;
    if(VMM_KADDR32_4(v.FirstPrototypePte)) {
        e->vaPrototypePte = v.FirstPrototypePte;
        e->cbPrototypePte = (DWORD)(v.LastContiguousPte - v.FirstPrototypePte);
    }
    return e;
}

PVMM_MAP_VADENTRY MmVad_Spider_MMVAD64_10(_In_ PVMM_PROCESS pSystemProcess, _In_ QWORD va, _In_ PVMMOB_MAP_VAD pmVad, _In_ POB_SET psAll, _In_ POB_SET psTry1, _In_opt_ POB_SET psTry2, _In_ QWORD fVmmRead, _In_ DWORD dwFlagsBitMask)
{
    _MMVAD64_10 v = { 0 };
    PVMM_MAP_VADENTRY e;
    if(!VmmRead2(pSystemProcess, va, (PBYTE)&v, sizeof(_MMVAD64_10), fVmmRead | VMM_FLAG_FORCECACHE_READ)) {
        ObSet_Push(psTry2, va);
        return NULL;
    }
    if((v.EndingVpnHigh < v.StartingVpnHigh) || (v.EndingVpn < v.StartingVpn) || !MmVad_Spider_PoolTagAny(v.PoolTag, 5, MMVAD_POOLTAG_VADS, MMVAD_POOLTAG_VAD, MMVAD_POOLTAG_VADL, MMVAD_POOLTAG_VADM, MMVAD_POOLTAG_VADF)) {
        return NULL;
    }
    // short vad
    e = &pmVad->pMap[pmVad->cMap++];
    if(VMM_KADDR64_16(v.Children[0])) {
        ObSet_Push(psAll, v.Children[0] - 0x10);
        ObSet_Push(psTry1, v.Children[0] - 0x10);
    }
    if(VMM_KADDR64_16(v.Children[1])) {
        ObSet_Push(psAll, v.Children[1] - 0x10);
        ObSet_Push(psTry1, v.Children[1] - 0x10);
    }
    e->vaStart = ((QWORD)v.StartingVpnHigh << (32 + 12)) | ((QWORD)v.StartingVpn << 12);
    e->vaEnd = ((QWORD)v.EndingVpnHigh << (32 + 12)) | ((QWORD)v.EndingVpn << 12) | 0xfff;
    e->CommitCharge = (DWORD)v.CommitCharge;
    e->MemCommit = (DWORD)v.MemCommit;
    e->VadType = 0x07 & (v.u >> (dwFlagsBitMask & 0xff));
    e->Protection = 0x1f & (v.u >> ((dwFlagsBitMask >> 8) & 0xff));
    e->fPrivateMemory = 0x01 & (v.u >> ((dwFlagsBitMask >> 16) & 0xff));
    // full vad
    if(v.PoolTag == MMVAD_POOLTAG_VADS) { return e; }
    e->flags[2] = (DWORD)v.u2;
    e->vaSubsection = v.Subsection;
    if(VMM_KADDR64_8(v.FirstPrototypePte)) {
        e->vaPrototypePte = v.FirstPrototypePte;
        e->cbPrototypePte = (DWORD)(v.LastContiguousPte - v.FirstPrototypePte + 8);
    }
    return e;
}

VOID MmVad_Spider_DoWork(_In_ PVMM_PROCESS pSystemProcess, _In_ PVMM_PROCESS pProcess, _In_ QWORD fVmmRead)
{
    BOOL f;
    QWORD i, va;
    DWORD cMax, cVads, dwFlagsBitMask = 0;
    PVMM_MAP_VADENTRY eVad;
    PVMMOB_MAP_VAD pmObVad = NULL, pmObVadTemp;
    POB_SET psObAll = NULL, psObTry1 = NULL, psObTry2 = NULL, psObPrefetch = NULL;
    PVMM_MAP_VADENTRY(*pfnMmVad_Spider)(PVMM_PROCESS, QWORD, PVMMOB_MAP_VAD, POB_SET, POB_SET, POB_SET, QWORD, DWORD);
    if(!(ctxVmm->tpSystem == VMM_SYSTEM_WINDOWS_X64 || ctxVmm->tpSystem == VMM_SYSTEM_WINDOWS_X86)) { goto fail; }
    // 1: retrieve # of VAD entries and sanity check.
    if(ctxVmm->kernel.dwVersionBuild >= 9600) {
        // Win8.1 and later -> fetch # of RtlBalancedNode from EPROCESS.
        cVads = (DWORD)VMM_EPROCESS_PTR(pProcess, ctxVmm->offset.EPROCESS.VadRoot + (ctxVmm->f32 ? 8 : 0x10));
    } else if(ctxVmm->kernel.dwVersionBuild >= 6000) {
        // WinVista::Win8.0 -> fetch # of AvlNode from EPROCESS.
        i = (ctxVmm->kernel.dwVersionBuild < 9200) ? (ctxVmm->f32 ? 0x14 : 0x28) : (ctxVmm->f32 ? 0x1c : 0x18);
        cVads = ((DWORD)VMM_EPROCESS_PTR(pProcess, ctxVmm->offset.EPROCESS.VadRoot + i) >> 8);
    } else {
        // WinXP
        cVads = (DWORD)VMM_EPROCESS_DWORD(pProcess, 0x240);
    }
    if(cVads > 0x1000) {
        vmmprintfv_fn("WARNING: BAD #VAD VALUE- PID: %i #VAD: %x\n", pProcess->dwPID, cVads);
        cVads = 0x1000;
    }
    // 2: allocate and retrieve objects required for processing
    if(!(pmObVad = Ob_Alloc(OB_TAG_MAP_VAD, LMEM_ZEROINIT, sizeof(VMMOB_MAP_VAD) + cVads * sizeof(VMM_MAP_VADENTRY), VmmVad_MemMapVad_CloseObCallback, NULL))) { goto fail; }
    if(cVads == 0) {    // No VADs
        vmmprintfvv_fn("WARNING: NO VAD FOR PROCESS - PID: %i STATE: %i NAME: %s\n", pProcess->dwPID, pProcess->dwState, pProcess->szName);
        pProcess->Map.pObVad = Ob_INCREF(pmObVad);
        goto fail;
    }
    cMax = cVads;
    if(!(psObAll = ObSet_New())) { goto fail; }
    if(!(psObTry1 = ObSet_New())) { goto fail; }
    if(!(psObTry2 = ObSet_New())) { goto fail; }
    // 3: retrieve initial VAD node entry
    f = ((ctxVmm->kernel.dwVersionBuild >= 6000) && (ctxVmm->kernel.dwVersionBuild < 9600));    // AvlTree (Vista::Win8.0
    for(i = (f ? 1 : 0); i < (f ? 4 : 1); i++) {
        va = VMM_EPROCESS_PTR(pProcess, ctxVmm->offset.EPROCESS.VadRoot + i * (ctxVmm->f32 ? 4 : 8));
        if(ctxVmm->f32 && !VMM_KADDR32_8(va)) { continue; }
        if(!ctxVmm->f32 && !VMM_KADDR64_16(va)) { continue; }
        va -= ctxVmm->f32 ? 8 : 0x10;
        ObSet_Push(psObAll, va);
        ObSet_Push(psObTry2, va);
    }
    if(!ObSet_Size(psObTry2)) { goto fail; }
    if(ctxVmm->kernel.dwVersionBuild >= 9600) {
        // Win8.1 and later
        pfnMmVad_Spider = ctxVmm->f32 ? MmVad_Spider_MMVAD32_10 : MmVad_Spider_MMVAD64_10;
        if(ctxVmm->kernel.dwVersionBuild >= 18362) {    // bitmask offset for empty:PrivateMemory:Protection:VadType
            dwFlagsBitMask = 0x00140704;
        } else if(ctxVmm->kernel.dwVersionBuild >= 17134) {
            dwFlagsBitMask = 0x000e0300;
        } else {
            dwFlagsBitMask = 0x000f0300;
        }
    } else if(ctxVmm->kernel.dwVersionBuild >= 9200) {
        // Win8.0
        pfnMmVad_Spider = ctxVmm->f32 ? MmVad_Spider_MMVAD32_80 : MmVad_Spider_MMVAD64_80;
    } else if(ctxVmm->kernel.dwVersionBuild >= 6000) {
        // WinVista :: Win7
        pfnMmVad_Spider = ctxVmm->f32 ? MmVad_Spider_MMVAD32_7 : MmVad_Spider_MMVAD64_7;
    } else {
        // WinXP
        pfnMmVad_Spider = MmVad_Spider_MMVAD32_XP;
    }
    // 4: cache: prefetch previous addresses
    if((psObPrefetch = ObContainer_GetOb(pProcess->pObPersistent->pObCMapVadPrefetch))) {
        VmmCachePrefetchPages3(pSystemProcess, psObPrefetch, sizeof(_MMVAD64_10), fVmmRead);
        Ob_DECREF_NULL(&psObPrefetch);
    }
    // 5: spider vad tree in an efficient way (minimize non-cached reads)
    while((pmObVad->cMap < cMax) && ObSet_Size(psObTry2)) {
        // fetch vad entries 2nd attempt
        VmmCachePrefetchPages3(pSystemProcess, psObTry2, sizeof(_MMVAD64_10), fVmmRead);
        while((pmObVad->cMap < cMax) && (va = ObSet_Pop(psObTry2))) {
            if((eVad = pfnMmVad_Spider(pSystemProcess, va, pmObVad, psObAll, psObTry1, NULL, fVmmRead, dwFlagsBitMask))) {
                if(eVad->CommitCharge > ((eVad->vaEnd + 1 - eVad->vaStart) >> 12)) { eVad->CommitCharge = 0; }
                eVad->vaVad = va + (ctxVmm->f32 ? 8 : 0x10);
                eVad->wszText = &ctxVmm->_EmptyWCHAR;
                if(eVad->cbPrototypePte > 0x01000000) { eVad->cbPrototypePte = MMVAD_PTESIZE * (DWORD)((0x1000 + eVad->vaEnd - eVad->vaStart) >> 12); }
            }
        }
        // fetch vad entries 1st attempt
        while((pmObVad->cMap < cMax) && (va = ObSet_Pop(psObTry1))) {
            if((eVad = pfnMmVad_Spider(pSystemProcess, va, pmObVad, psObAll, psObTry1, psObTry2, fVmmRead, dwFlagsBitMask))) {
                if(eVad->CommitCharge > ((eVad->vaEnd + 1 - eVad->vaStart) >> 12)) { eVad->CommitCharge = 0; }
                eVad->vaVad = va + (ctxVmm->f32 ? 8 : 0x10);
                eVad->wszText = &ctxVmm->_EmptyWCHAR;
                if(eVad->cbPrototypePte > 0x01000000) { eVad->cbPrototypePte = MMVAD_PTESIZE * (DWORD)((0x1000 + eVad->vaEnd - eVad->vaStart) >> 12); }
            }
        }
    }
    // 6: sort result
    if(pmObVad->cMap > 1) {
        qsort(pmObVad->pMap, pmObVad->cMap, sizeof(VMM_MAP_VADENTRY), MmVad_CmpVadEntry);
    }
    // 7: cache: update
    ObContainer_SetOb(pProcess->pObPersistent->pObCMapVadPrefetch, psObAll);
    // 8: shrink oversized result object (if sufficiently too large)
    if(pmObVad->cMap + 0x10 < cMax) {
        pmObVadTemp = pmObVad;
        if(!(pmObVad = Ob_Alloc(OB_TAG_MAP_VAD, 0, sizeof(VMMOB_MAP_VAD) + pmObVadTemp->cMap * sizeof(VMM_MAP_VADENTRY), VmmVad_MemMapVad_CloseObCallback, NULL))) { goto fail; }
        memcpy(((POB_DATA)pmObVad)->pb, ((POB_DATA)pmObVadTemp)->pb, pmObVad->ObHdr.cbData);
        Ob_DECREF_NULL(&pmObVadTemp);
    }
    pProcess->Map.pObVad = Ob_INCREF(pmObVad);
fail:
    Ob_DECREF(pmObVad);
    Ob_DECREF(psObAll);
    Ob_DECREF(psObTry1);
    Ob_DECREF(psObTry2);
}

/*
* Fetch extended information such as file and image names into the buffer
* pProcess->pObMemMapVad->wszText which will be allocated by the function
* and must be free'd upon cleanup of pObMemMapVad.
* NB! MUST BE CALLED IN THREAD-SAFE WAY AND MUST NOT HAVE A PREVIOUS BUFFER!
* -- pSystemProcess
* -- pProcess
* -- fVmmRead
*/
VOID MmVad_TextFetch(_In_ PVMM_PROCESS pSystemProcess, _In_ PVMM_PROCESS pProcess, _In_ QWORD fVmmRead)
{
    BOOL fResult = FALSE;
    BOOL f, f32 = ctxVmm->f32, fSharedCacheMap = FALSE;
    WORD oControlArea_FilePointer, oControlArea_SegmentPointer, oSegment_SizeOfSegment;
    DWORD cMax, oMultiText = 1, cwszMultiText = 2, dwTID;
    BYTE pb[MAX_PATH*2+2], *pb2;
    PQWORD pva = NULL;
    LPWSTR wszMultiText = NULL;
    QWORD i, j, va, cVads = 0;
    PVMM_MAP_VADENTRY pVad, *ppVads;
    PVMMOB_MAP_HEAP pObHeapMap = NULL;
    PVMMOB_MAP_THREAD pObThreadMap = NULL;
    VmmMap_GetThreadAsync(pProcess);        // thread map async initialization to speed up later retrieval.
    // count max potential vads and allocate.
    {
        for(i = 0, cMax = pProcess->Map.pObVad->cMap; i < cMax; i++) {
            va = pProcess->Map.pObVad->pMap[i].vaSubsection;
            if(VMM_KADDR_4_8(va)) {
                cVads++;
            }
        }
        if(!cVads || !(pva = LocalAlloc(LMEM_ZEROINIT, cVads * 0x18))) { goto fail; }
        ppVads = (PVMM_MAP_VADENTRY*)(pva + 2 * cVads);
    }
    // get subsection addresses from vad.
    {
        for(i = 0, j = 0, cMax = pProcess->Map.pObVad->cMap; (i < cMax) && (j < cVads); i++) {
            va = pProcess->Map.pObVad->pMap[i].vaSubsection;
            if(VMM_KADDR_4_8(va)) {
                ppVads[j] = pProcess->Map.pObVad->pMap + i;
                pva[j++] = va;
            }
        }
    }
    // fetch subsection -> pointer to control area (1st address ptr in subsection)
    if((ctxVmm->kernel.dwVersionBuild >= 6000)) {   // Not WinXP (ControlArea already in map subsection field).
        VmmCachePrefetchPages4(pSystemProcess, (DWORD)cVads, pva, 8, fVmmRead);
        for(i = 0, va = 0; i < cVads; i++) {
            f = pva[i] &&
                VmmRead2(pSystemProcess, pva[i], (PBYTE)&va, f32 ? 4 : 8, fVmmRead | VMM_FLAG_FORCECACHE_READ) &&
                VMM_KADDR_8_16(va);
            pva[i] = f ? (va - 0x10) : 0;
        }
    }
    // fetch _CONTROL_AREA -> pointer to _FILE_OBJECT
    {
        VmmCachePrefetchPages4(pSystemProcess, (DWORD)cVads, pva, 0x50, fVmmRead);
        oControlArea_FilePointer = f32 ?
            ((ctxVmm->kernel.dwVersionBuild <= 7601) ? 0x24 : 0x20) :   // 32-bit win7sp1- or win8.0+
            ((ctxVmm->kernel.dwVersionBuild <= 6000) ? 0x30 : 0x40);    // 64-bit vistasp0- or vistasp1+
        oControlArea_SegmentPointer = 0;
        oSegment_SizeOfSegment = f32 ? 0x10 : 0x18;
        for(i = 0; i < cVads; i++) {
            // pointer to _FILE_OBJECT
            f = pva[i] &&
                VmmRead2(pSystemProcess, pva[i], pb, 0x60, fVmmRead | VMM_FLAG_FORCECACHE_READ) &&
                (VMM_POOLTAG_PREPENDED(pb, 0x10, 'MmCa') || VMM_POOLTAG_PREPENDED(pb, 0x10, 'MmCi')) &&
                (va = VMM_PTR_OFFSET_EX_FAST_REF(f32, pb + 0x10, oControlArea_FilePointer)) &&
                VMM_KADDR_8_16(va);
            if(pva[i] && !f && VMM_POOLTAG_PREPENDED(pb, 0x10, 'MmCa')) { ppVads[i]->fPageFile = 1; }
            if(f && VMM_POOLTAG_PREPENDED(pb, 0x10, 'MmCa')) {
                ppVads[i]->fFile = 1;
                ppVads[i]->vaFileObject = va;
            }
            if(f && VMM_POOLTAG_PREPENDED(pb, 0x10, 'MmCi')) {
                ppVads[i]->fImage = 1;
                ppVads[i]->vaFileObject = va;
            }
            pva[i] = f ? va : 0;
        }
    }
    // fetch _FILE_OBJECT -> _UNICODE_STRING (size and ptr to text)
    {
        pb2 = pb + (f32 ? 0x30 : 0x58);     // pb2 = offset into _FILE_OBJECT.FileName _UNICODE_STRING (pb)
        VmmCachePrefetchPages4(pSystemProcess, (DWORD)cVads, pva, 0x68, fVmmRead);
        for(i = 0, va = 0; i < cVads; i++) {
            // fetch _FILE_OBJECT
            f = pva[i] &&
                VmmRead2(pSystemProcess, pva[i], pb, 0x68, fVmmRead | VMM_FLAG_FORCECACHE_READ) &&
                *(PWORD)(pb2) && (*(PWORD)(pb2) <= *(PWORD)(pb2 + 2)) &&
                (va = f32 ? *(PDWORD)(pb2 + 4) : *(PQWORD)(pb2 + 8)) &&
                VMM_KADDR_8_16(va);
            pva[i] = f ? va : 0;    // PTR FileName _UNICODE_STRING.Buffer
            if(f) {
                // _FILE_OBJECT->FileName _UNICODE_STRING.Length
                ppVads[i]->cwszText = min(0xff, *(PWORD)(pb2) >> 1);
                cwszMultiText += ppVads[i]->cwszText + 1;
            }
        }
    }
    // [ heap map fetch reference ]
    if(VmmMap_GetHeap(pProcess, &pObHeapMap)) {
        cwszMultiText += 8 * pObHeapMap->cMap;      // 7 WCHAR + NULL per heap entry
    }
    // [ thread map fetch reference ]
    if(VmmMap_GetThread(pProcess, &pObThreadMap)) {
        cwszMultiText += 20 * pObThreadMap->cMap;   // 8(TEB) + 10(STACK) WCHAR + 2 NULL per thread entry
    }
    // fetch and parse: _UNICODE_STRING.Buffer
    {
        if(!(wszMultiText = LocalAlloc(LMEM_ZEROINIT, (QWORD)cwszMultiText << 1))) { goto fail; }
        VmmCachePrefetchPages4(pSystemProcess, (DWORD)cVads * 2, pva, MAX_PATH * 2, fVmmRead);
        for(i = 0; i < cVads; i++) {
            // _UNICODE_STRING.Buffer
            f = pva[i] && VmmRead2(pSystemProcess, pva[i], (PBYTE)(wszMultiText + oMultiText), ppVads[i]->cwszText << 1, fVmmRead | VMM_FLAG_FORCECACHE_READ);
            if(f) {
                ppVads[i]->wszText = wszMultiText + oMultiText;
                oMultiText += 1 + ppVads[i]->cwszText;
            } else {
                ppVads[i]->wszText = wszMultiText;
            }
        }
    }
    // [ heap map parse ]
    if(pObHeapMap) {
        for(i = 0; i < pObHeapMap->cMap; i++) {
            if((pVad = VmmMap_GetVadEntry(pProcess->Map.pObVad, pObHeapMap->pMap[i].vaHeapSegment))) {
                pVad->fHeap = 1;
                pVad->HeapNum = pObHeapMap->pMap[i].HeapId;
                if(!pVad->cwszText) {
                    swprintf_s(wszMultiText + oMultiText, cwszMultiText - oMultiText, L"HEAP-%02X", pVad->HeapNum);
                    pVad->wszText = wszMultiText + oMultiText;
                    pVad->cwszText = 7;
                    oMultiText += 8;
                }
            }
        }
    }
    // [ thread map parse ]
    if(pObThreadMap) {
        for(i = 0; i < pObThreadMap->cMap; i++) {
            if((pVad = VmmMap_GetVadEntry(pProcess->Map.pObVad, pObThreadMap->pMap[i].vaTeb))) {
                pVad->fTeb = TRUE;
                if(!pVad->cwszText) {
                    dwTID = min(0xffff, pObThreadMap->pMap[i].dwTID);
                    swprintf_s(wszMultiText + oMultiText, cwszMultiText - oMultiText, L"TEB-%04X", (WORD)min(0xffff, pObThreadMap->pMap[i].dwTID));
                    pVad->wszText = wszMultiText + oMultiText;
                    pVad->cwszText = 8;
                    oMultiText += 9;
                }
            }
            if((pVad = VmmMap_GetVadEntry(pProcess->Map.pObVad, pObThreadMap->pMap[i].vaStackLimitUser))) {
                pVad->fStack = TRUE;
                if(!pVad->cwszText) {
                    dwTID = min(0xffff, pObThreadMap->pMap[i].dwTID);
                    swprintf_s(wszMultiText + oMultiText, cwszMultiText - oMultiText, L"STACK-%04X", (WORD)min(0xffff, pObThreadMap->pMap[i].dwTID));
                    pVad->wszText = wszMultiText + oMultiText;
                    pVad->cwszText = 10;
                    oMultiText += 11;
                }
            }
        }
    }
    // cleanup
    pProcess->Map.pObVad->cbMultiText = cwszMultiText << 1;
    pProcess->Map.pObVad->wszMultiText = wszMultiText;
    fResult = TRUE;
fail:
    if(!fResult) { LocalFree(wszMultiText); }
    Ob_DECREF(pObThreadMap);
    Ob_DECREF(pObHeapMap);
    LocalFree(pva);
}

_Success_(return)
BOOL MmVad_PrototypePteArray_FetchNew_PoolHdrVerify(_In_ PBYTE pb, _In_ DWORD cbDataOffsetPoolHdr)
{
    DWORD o;
    if(cbDataOffsetPoolHdr < 0x10) {
        return !cbDataOffsetPoolHdr || ('tSmM' == *(PDWORD)pb);
    }
    for(o = 0; o < cbDataOffsetPoolHdr; o += 4) {
        if('tSmM' == *(PDWORD)(pb + o)) { return TRUE; }    // check for MmSt pool header in various locations
    }
    return FALSE;
}

/*
* Fetch an array of prototype pte's into the cache.
* -- pSystemProcess
* -- pVad
* -- fVmmRead
*/
VOID MmVad_PrototypePteArray_FetchNew(_In_ PVMM_PROCESS pSystemProcess, _In_ PVMM_MAP_VADENTRY pVad, _In_ QWORD fVmmRead)
{
    PBYTE pbData;
    POB_DATA e = NULL;
    DWORD cbData, cbDataOffsetPoolHdr = 0;
    cbData = pVad->cbPrototypePte;
    // 1: santity check size
    if(cbData > 0x00010000) {   // most probably an error, file > 32MB
        cbData = MMVAD_PTESIZE * (DWORD)((0x1000 + pVad->vaEnd - pVad->vaStart) >> 12);
        if(cbData > 0x00010000) { return; }
    }
    // 2: pool header offset (if any)
    if(pVad->vaPrototypePte & 0xfff) {
        if(ctxVmm->kernel.dwVersionBuild >= 9200) {             // WIN8.0 and later
            cbDataOffsetPoolHdr = ctxVmm->f32 ? 0x04 : 0x0c;
        } else {
            // WinXP to Win7 - pool header seems to be varying between these zero and these offsets, check for them all...
            cbDataOffsetPoolHdr = ctxVmm->f32 ? 0x34 : 0x5c;
            if((pVad->vaStart & 0xfff) < cbDataOffsetPoolHdr) { cbDataOffsetPoolHdr = 0; }
        }
        cbData += cbDataOffsetPoolHdr;
    }
    // 3: fetch prototype page table entries
    if(!(pbData = LocalAlloc(0, cbData))) { return; }
    if(VmmRead2(pSystemProcess, pVad->vaPrototypePte - cbDataOffsetPoolHdr, pbData, cbData, fVmmRead)) {
        if(MmVad_PrototypePteArray_FetchNew_PoolHdrVerify(pbData, cbDataOffsetPoolHdr)) {
            if((e = Ob_Alloc('MmSt', 0, sizeof(OB) + cbData - cbDataOffsetPoolHdr, NULL, NULL))) {
                memcpy(e->pb, pbData + cbDataOffsetPoolHdr, cbData - cbDataOffsetPoolHdr);
            }
        }
    }
    if(!e) {
        e = Ob_Alloc('MmSt', 0, sizeof(OB), NULL, NULL);
    }
    if(e) {
        ObMap_Push(ctxVmm->Cache.pmPrototypePte, pVad->vaPrototypePte, e);
        Ob_DECREF(e);
    }
    LocalFree(pbData);
}

/*
* Retrieve an object manager object containing the prototype pte's. THe object
* will be retrieved from cache if possible, otherwise a read will be attempted
* provided that the fVmmRead flags allows for it.
* CALLER DECREF: return
* -- pVad
* -- fVmmRead
* -- return
*/
POB_DATA MmVad_PrototypePteArray_Get(_In_ PVMM_PROCESS pProcess, _In_ PVMM_MAP_VADENTRY pVad, _In_ QWORD fVmmRead)
{
    QWORD i, va;
    POB_DATA e = NULL;
    POB_SET psObPrefetch = NULL;
    PVMM_PROCESS pObSystemProcess = NULL;
    PVMMOB_MAP_VAD pVadMap;
    if(!pVad->vaPrototypePte || !pVad->cbPrototypePte) { return NULL; }
    if((e = ObMap_GetByKey(ctxVmm->Cache.pmPrototypePte, pVad->vaPrototypePte))) { return e; }
    EnterCriticalSection(&pProcess->LockUpdate);
    if((e = ObMap_GetByKey(ctxVmm->Cache.pmPrototypePte, pVad->vaPrototypePte))) {
        LeaveCriticalSection(&pProcess->LockUpdate);
        return e;
    }
    if((pObSystemProcess = VmmProcessGet(4))) {
        if(!pProcess->Map.pObVad->fSpiderPrototypePte && pVad->cbPrototypePte < 0x1000 && (psObPrefetch = ObSet_New())) {
            pVadMap = pProcess->Map.pObVad;
            // spider all prototype pte's less than 0x1000 in size into the cache
            pVadMap->fSpiderPrototypePte = TRUE;
            for(i = 0; i < pVadMap->cMap; i++) {
                va = pVadMap->pMap[i].vaPrototypePte;
                if(va && (pVadMap->pMap[i].cbPrototypePte < 0x1000) && !ObMap_ExistsKey(ctxVmm->Cache.pmPrototypePte, va)) {
                    ObSet_Push(psObPrefetch, va);
                }
            }
            VmmCachePrefetchPages3(pObSystemProcess, psObPrefetch, 0x1000, fVmmRead);
            for(i = 0; i < pVadMap->cMap; i++) {
                va = pVadMap->pMap[i].vaPrototypePte;
                if(va && (pVadMap->pMap[i].cbPrototypePte < 0x1000) && !ObMap_ExistsKey(ctxVmm->Cache.pmPrototypePte, va)) {
                    MmVad_PrototypePteArray_FetchNew(pObSystemProcess, pVadMap->pMap + i, fVmmRead | VMM_FLAG_FORCECACHE_READ);
                }
            }
            Ob_DECREF(psObPrefetch);
        } else {
            // fetch single vad prototypte pte array into the cache
            MmVad_PrototypePteArray_FetchNew(pObSystemProcess, pVad, fVmmRead);
        }
        Ob_DECREF(pObSystemProcess);
    }
    LeaveCriticalSection(&pProcess->LockUpdate);
    return ObMap_GetByKey(ctxVmm->Cache.pmPrototypePte, pVad->vaPrototypePte);
}



// ----------------------------------------------------------------------------
// IMPLEMENTATION OF VAD RELATED GENERAL FUNCTIONALITY BELOW:
// ----------------------------------------------------------------------------

/*
* Try to read a prototype page table entry (PTE).
* -- pProcess
* -- va
* -- pfInRange
* -- fVmmRead = VMM_FLAGS_* flags.
* -- return = prototype pte or zero on fail.
*/
QWORD MmVad_PrototypePte(_In_ PVMM_PROCESS pProcess, _In_ QWORD va, _Out_opt_ PBOOL pfInRange, _In_ QWORD fVmmRead)
{
    QWORD iPrototypePte, qwPrototypePte = 0;
    POB_DATA pObPteArray = NULL;
    PVMM_MAP_VADENTRY pVad = NULL;
    if(MmVad_MapInitialize(pProcess, FALSE, fVmmRead) && (pVad = VmmMap_GetVadEntry(pProcess->Map.pObVad, va)) && (pObPteArray = MmVad_PrototypePteArray_Get(pProcess, pVad, fVmmRead))) {
        iPrototypePte = (va - pVad->vaStart) >> 12;
        if(ctxVmm->tpMemoryModel == VMM_MEMORYMODEL_X86) {
            if(pObPteArray->ObHdr.cbData > (iPrototypePte * 4)) {
                qwPrototypePte = pObPteArray->pdw[iPrototypePte];
            }
        } else {
            if(pObPteArray->ObHdr.cbData > (iPrototypePte * 8)) {
                qwPrototypePte = pObPteArray->pqw[iPrototypePte];
            }
        }
        Ob_DECREF(pObPteArray);
    }
    if(pfInRange) { *pfInRange = pVad ? TRUE : FALSE; }
    return qwPrototypePte;
}

_Success_(return)
BOOL MmVad_MapInitialize_Core(_In_ PVMM_PROCESS pProcess, _In_ QWORD fVmmRead)
{
    PVMM_PROCESS pObSystemProcess;
    if(pProcess->Map.pObVad) { return TRUE; }
    EnterCriticalSection(&pProcess->LockUpdate);
    if(!pProcess->Map.pObVad && (pObSystemProcess = VmmProcessGet(4))) {
        MmVad_Spider_DoWork(pObSystemProcess, pProcess, fVmmRead | VMM_FLAG_NOVAD);
        if(!pProcess->Map.pObVad) {
            pProcess->Map.pObVad = Ob_Alloc(OB_TAG_MAP_VAD, LMEM_ZEROINIT, sizeof(VMMOB_MAP_VAD), VmmVad_MemMapVad_CloseObCallback, NULL);
        }
        Ob_DECREF(pObSystemProcess);
    }
    LeaveCriticalSection(&pProcess->LockUpdate);
    return pProcess->Map.pObVad ? TRUE : FALSE;
}

_Success_(return)
BOOL MmVad_MapInitialize_Text(_In_ PVMM_PROCESS pProcess, _In_ QWORD fVmmRead)
{
    PVMM_PROCESS pObSystemProcess;
    if(pProcess->Map.pObVad->wszMultiText) { return TRUE; }
    EnterCriticalSection(&pProcess->Map.LockUpdateExtendedInfo);
    if(!pProcess->Map.pObVad->wszMultiText && (pObSystemProcess = VmmProcessGet(4))) {
        MmVad_TextFetch(pObSystemProcess, pProcess, fVmmRead | VMM_FLAG_NOVAD);
        Ob_DECREF(pObSystemProcess);
    }
    LeaveCriticalSection(&pProcess->Map.LockUpdateExtendedInfo);
    return pProcess->Map.pObVad->wszMultiText ? TRUE : FALSE;
}

/*
* Initialize / Ensure that a VAD map is initialized for the specific process.
* -- pProcess
* -- fExtendedText = also fetch extended info such as module names.
* -- fVmmRead = VMM_FLAGS_* flags.
* -- return
*/
_Success_(return)
BOOL MmVad_MapInitialize(_In_ PVMM_PROCESS pProcess, _In_ BOOL fExtendedText, _In_ QWORD fVmmRead)
{
    if(pProcess->Map.pObVad && (!fExtendedText || pProcess->Map.pObVad->wszMultiText)) { return TRUE; }
    VmmTlbSpider(pProcess);
    return MmVad_MapInitialize_Core(pProcess, fVmmRead) && (!fExtendedText || MmVad_MapInitialize_Text(pProcess, fVmmRead));
}
