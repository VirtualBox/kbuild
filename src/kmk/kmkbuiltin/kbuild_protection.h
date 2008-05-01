/* $Id: kbuild_protection.h 1598 2008-05-01 21:52:59Z knut.osmundsen@oracle.com $ */
/** @file
 * Simple File Protection.
 */

/*
 * Copyright (c) 2008 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef ___kbuild_protection_h___
#define ___kbuild_protection_h___

/**
 * The different protection types.
 */
typedef enum
{
    KBUILDPROTECTIONTYPE_FIRST = 0,
    KBUILDPROTECTIONTYPE_RECURSIVE = KBUILDPROTECTIONTYPE_FIRST,
    KBUILDPROTECTIONTYPE_FULL,
    KBUILDPROTECTIONTYPE_MAX
} KBUILDPROTECTIONTYPE;


/**
 * The instance data.
 * Don't touch.
 */
typedef struct KBUILDPROTECTION
{
    unsigned        uMagic;
    unsigned        cProtectionDepth;
    unsigned char   afTypes[KBUILDPROTECTIONTYPE_MAX];
} KBUILDPROTECTION;
typedef KBUILDPROTECTION  *PKBUILDPROTECTION;
typedef const KBUILDPROTECTION *PCKBUILDPROTECTION;


void kBuildProtectionInit(PKBUILDPROTECTION pThis);
void kBuildProtectionTerm(PKBUILDPROTECTION pThis);
int  kBuildProtectionScanEnv(PKBUILDPROTECTION pThis, char **papszEnv, const char *pszPrefix);
void kBuildProtectionEnable(PKBUILDPROTECTION pThis, KBUILDPROTECTIONTYPE enmType);
void kBuildProtectionDisable(PKBUILDPROTECTION pThis, KBUILDPROTECTIONTYPE enmType);
int  kBuildProtectionSetDepth(PKBUILDPROTECTION pThis, const char *pszValue);
int  kBuildProtectionEnforce(PCKBUILDPROTECTION pThis, KBUILDPROTECTIONTYPE enmType, const char *pszPath);
int  kBuildProtectionDefaultDepth(void);

#endif

