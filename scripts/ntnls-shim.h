#pragma once
// Minimal compatibility declaration for the two NLS structures LEP uses.
// The Windows Driver Kit normally supplies <ntnls.h>; this project does not
// use any other WDK header. Layout follows the public CPTABLEINFO/NLSTABLEINFO
// definitions used by ntdll (MAXIMUM_LEADBYTES = 12).
#ifndef CLAUDE_PROTECT_NTNLS_SHIM_H
#define CLAUDE_PROTECT_NTNLS_SHIM_H

#ifndef MAXIMUM_LEADBYTES
#define MAXIMUM_LEADBYTES 12
#endif

typedef struct _CPTABLEINFO {
    USHORT CodePage;
    USHORT MaximumCharacterSize;
    USHORT DefaultChar;
    USHORT UniDefaultChar;
    USHORT TransDefaultChar;
    USHORT TransUniDefaultChar;
    USHORT DBCSCodePage;
    UCHAR LeadByte[MAXIMUM_LEADBYTES];
    PUSHORT MultiByteTable;
    PVOID WideCharTable;
    PUSHORT DBCSRanges;
    PUSHORT DBCSOffsets;
} CPTABLEINFO, *PCPTABLEINFO;

typedef struct _NLSTABLEINFO {
    CPTABLEINFO OemTableInfo;
    CPTABLEINFO AnsiTableInfo;
    PUSHORT UpperCaseTable;
    PUSHORT LowerCaseTable;
} NLSTABLEINFO, *PNLSTABLEINFO;

#endif
