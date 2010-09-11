/* $Id: kbuild_protection.h 2413 2010-09-11 17:43:04Z knut.osmundsen@oracle.com $ */
/** @file
 * Simple File Protection.
 */

/*
 * Copyright (c) 2008-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild.  If not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef ___kbuild_protection_h
#define ___kbuild_protection_h

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
    unsigned int    uMagic;
    unsigned int    cProtectionDepth;
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

