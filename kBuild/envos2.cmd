/*
echo this is a rexx script!
cancel & quit & exit
*/
/* $Id: $ */
/** @file
 *
 * Environment setup script for OS/2.
 *
 * Copyright (c) 1999-2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */


/*
 * Setup the usual suspects.
 */
Address CMD '@echo off';
signal on novalue name NoValueHandler
if (RxFuncQuery('SysLoadFuncs') = 1) then
do
    call RxFuncAdd 'SysLoadFuncs', 'RexxUtil', 'SysLoadFuncs';
    call SysLoadFuncs;
end

/*
 * Apply the CMD.EXE workaround.
 */
call FixCMDEnv;

/*
 * Establish the kBuild environment variables.
 */

/* kBuild path. */
if (EnvGet("PATH_KBUILD") = "") then
do
    call EnvSet 0, "PATH_KBUILD", GetScriptDir();
end
if (  FileExists(EnvGet("PATH_KBUILD")||"\footer.kmk") = 0,
    | FileExists(EnvGet("PATH_KBUILD")||"\header.kmk") = 0,
    | FileExists(EnvGet("PATH_KBUILD")||"\rules.kmk") = 0) then
do
    say "kBuild: error: PATH_KBUILD ("||EnvGet("PATH_KBUILD")||") is not point to a populated kBuild directory.";
    exit(1);
end
say "dbg: PATH_KBUILD="||EnvGet("PATH_KBUILD");

/* Type. */
if (EnvGet("BUILD_TYPE") = "") then
    call EnvSet 0, "BUILD_TYPE", "release";
call EnvSet 0, "BUILD_TYPE", ToLower(EnvGet("BUILD_TYPE"));
say "dbg: BUILD_TYPE="||EnvGet("BUILD_TYPE");


/* Host platform. */
if (EnvGet("BUILD_PLATFORM_CPU") = "") then
    call EnvSet 0, "BUILD_PLATFORM_CPU", "blend";
call EnvSet 0, "BUILD_PLATFORM_CPU", ToLower(EnvGet("BUILD_PLATFORM_CPU"));
say "dbg: BUILD_PLATFORM_CPU="||EnvGet("BUILD_PLATFORM_CPU");

if (EnvGet("BUILD_PLATFORM_ARCH") = "") then
    call EnvSet 0, "BUILD_PLATFORM_ARCH", "x86";
call EnvSet 0, "BUILD_PLATFORM_ARCH", ToLower(EnvGet("BUILD_PLATFORM_ARCH"));
say "dbg: BUILD_PLATFORM_ARCH="||EnvGet("BUILD_PLATFORM_ARCH");

if (EnvGet("BUILD_PLATFORM") = "") then
    call EnvSet 0, "BUILD_PLATFORM", "os2";
call EnvSet 0, "BUILD_PLATFORM", ToLower(EnvGet("BUILD_PLATFORM"));
say "dbg: BUILD_PLATFORM="||EnvGet("BUILD_PLATFORM");


/* Target platform. */
if (  (  EnvGet("BUILD_TARGET") = "",
       | EnvGet("BUILD_TARGET") = EnvGet("BUILD_PLATFORM")),
    & (  EnvGet("BUILD_TARGET_ARCH") = "",
       | EnvGet("BUILD_TARGET_ARCH") = EnvGet("BUILD_PLATFORM_ARCH")),
    & (  EnvGet("BUILD_TARGET_CPU") = "", 
       | EnvGet("BUILD_TARGET_CPU") = "blend")) then
do
    call EnvSet 0, "BUILD_TARGET", EnvGet("BUILD_PLATFORM");
    call EnvSet 0, "BUILD_TARGET_ARCH", EnvGet("BUILD_PLATFORM_ARCH");
    call EnvSet 0, "BUILD_TARGET_CPU", EnvGet("BUILD_PLATFORM_CPU");
end
if (  EnvGet("BUILD_TARGET") <> "",
    & EnvGet("BUILD_TARGET_ARCH") = "",
    & (  EnvGet("BUILD_TARGET_CPU") = "", 
       | EnvGet("BUILD_TARGET_CPU") = "blend")) then
do
    select
        when ( EnvGet("BUILD_TARGET") = "os2" ) then 
        do
            call EnvSet 0, "BUILD_TARGET_ARCH", "x86";
            call EnvSet 0, "BUILD_TARGET_CPU", "blend";
        end
        
        otherwise
            say "kBuild: error: can't figure out target arch/cpu from the BUILD_TARGET value ("||EnvGet("BUILD_TARGET")||")";
            exit(1);
    end
end
if (  EnvGet("BUILD_TARGET") <> "",
    & EnvGet("BUILD_TARGET_ARCH") <> "",
    & EnvGet("BUILD_TARGET_CPU") = "") then
do
    call EnvSet 0, "BUILD_TARGET_CPU", "blend";
end
if (  EnvGet("BUILD_TARGET_ARCH") = "",
    | EnvGet("BUILD_TARGET_CPU") = "",
    | EnvGet("BUILD_TARGET") = "") then
do
    say "kBuild: error: can't figure out all the target settings, try specify all three."
    say "kBuild:  info: BUILD_TARGET="||EnvGet("BUILD_TARGET")
    say "kBuild:  info: BUILD_TARGET_CPU="||EnvGet("BUILD_TARGET_CPU")
    say "kBuild:  info: BUILD_TARGET_ARCH="||EnvGet("BUILD_TARGET_ARCH")
    exit(1);
end

say "dbg: BUILD_TARGET="||EnvGet("BUILD_TARGET");
say "dbg: BUILD_TARGET_ARCH="||EnvGet("BUILD_TARGET_ARCH");
say "dbg: BUILD_TARGET_CPU="||EnvGet("BUILD_TARGET_CPU");


sPlatformBin = EnvGet("PATH_KBUILD")||"\bin\"||EnvGet("BUILD_PLATFORM")||"."||EnvGet("BUILD_PLATFORM_ARCH");

/* Make shell */
call EnvSet 0, "MAKESHELL", translate(sPlatformBin||"/kmk_ash.exe", '/', '\');

/* The PATH. */
call EnvAddFront 0, "PATH", sPlatformBin;
say "dbg: PATH="||EnvGet("PATH");

/* The BEGINLIBPATH. */
call EnvAddFront 0, "BEGINLIBPATH", sPlatformBin;
say "dbg: BEGINLIBPATH="||EnvGet("BEGINLIBPATH");

/* Sanity check */
if (DirExists(sPlatformBin) = 0) then
do
    say "kBuild: warning: The bin directory for this platform doesn't exist. ("||sPlatformBin||")";
end
else
do
    sPrograms = "kmk kDepPre kDepIDB kmk_append kmk_ash kmk_cat kmk_cp kmk_echo kmk_install kmk_ln kmk_mkdir kmk_mv kmk_rm kmk_rmdir kmk_sed";
    do i = 1 to words(sPrograms)
        sProgram = word(sPrograms, i);
        if (FileExists(sPlatformBin||"\"||sProgram||".exe") = 0) then
        do
            say "kBuild: warning: The "||sProgram||" program doesn't exit for this platform. ("||sPlatformBin||")";
        end
    end
end


/*
 * Execute command if any arguments was given.
 */
parse arg sArgs
if (strip(sArgs) <> "") then
do
    sArgs;
    exit(rc);
end
exit(0);


/*******************************************************************************
*   Procedure Section                                                          *
*******************************************************************************/

/**
 * Give the script syntax
 */
syntax: procedure
    say 'syntax: envos2.cmd [command to be executed and its arguments]'
    say ''
return 0;


/**
 * No value handler
 */
NoValueHandler:
    say 'NoValueHandler: line 'SIGL;
exit(16);



/**
 * Add sToAdd in front of sEnvVar.
 * Note: sToAdd now is allowed to be alist!
 *
 * Known features: Don't remove sToAdd from original value if sToAdd
 *                 is at the end and don't end with a ';'.
 */
EnvAddFront: procedure
    parse arg fRM, sEnvVar, sToAdd, sSeparator

    /* sets default separator if not specified. */
    if (sSeparator = '') then sSeparator = ';';

    /* checks that sToAdd ends with an ';'. Adds one if not. */
    if (substr(sToAdd, length(sToAdd), 1) <> sSeparator) then
        sToAdd = sToAdd || sSeparator;

    /* check and evt. remove ';' at start of sToAdd */
    if (substr(sToAdd, 1, 1) = ';') then
        sToAdd = substr(sToAdd, 2);

    /* loop thru sToAdd */
    rc = 0;
    i = length(sToAdd);
    do while i > 1 & rc = 0
        j = lastpos(sSeparator, sToAdd, i-1);
        rc = EnvAddFront2(fRM, sEnvVar, substr(sToAdd, j+1, i - j), sSeparator);
        i = j;
    end

return rc;

/**
 * Add sToAdd in front of sEnvVar.
 *
 * Known features: Don't remove sToAdd from original value if sToAdd
 *                 is at the end and don't end with a ';'.
 */
EnvAddFront2: procedure
    parse arg fRM, sEnvVar, sToAdd, sSeparator

    /* sets default separator if not specified. */
    if (sSeparator = '') then sSeparator = ';';

    /* checks that sToAdd ends with a separator. Adds one if not. */
    if (substr(sToAdd, length(sToAdd), 1) <> sSeparator) then
        sToAdd = sToAdd || sSeparator;

    /* check and evt. remove the separator at start of sToAdd */
    if (substr(sToAdd, 1, 1) = sSeparator) then
        sToAdd = substr(sToAdd, 2);

    /* Get original variable value */
    sOrgEnvVar = EnvGet(sEnvVar);

    /* Remove previously sToAdd if exists. (Changing sOrgEnvVar). */
    i = pos(translate(sToAdd), translate(sOrgEnvVar));
    if (i > 0) then
        sOrgEnvVar = substr(sOrgEnvVar, 1, i-1) || substr(sOrgEnvVar, i + length(sToAdd));

    /* set environment */
    if (fRM) then
        return EnvSet(0, sEnvVar, sOrgEnvVar);
return EnvSet(0, sEnvVar, sToAdd||sOrgEnvVar);


/**
 * Add sToAdd as the end of sEnvVar.
 * Note: sToAdd now is allowed to be alist!
 *
 * Known features: Don't remove sToAdd from original value if sToAdd
 *                 is at the end and don't end with a ';'.
 */
EnvAddEnd: procedure
    parse arg fRM, sEnvVar, sToAdd, sSeparator

    /* sets default separator if not specified. */
    if (sSeparator = '') then sSeparator = ';';

    /* checks that sToAdd ends with a separator. Adds one if not. */
    if (substr(sToAdd, length(sToAdd), 1) <> sSeparator) then
        sToAdd = sToAdd || sSeparator;

    /* check and evt. remove ';' at start of sToAdd */
    if (substr(sToAdd, 1, 1) = sSeparator) then
        sToAdd = substr(sToAdd, 2);

    /* loop thru sToAdd */
    rc = 0;
    i = length(sToAdd);
    do while i > 1 & rc = 0
        j = lastpos(sSeparator, sToAdd, i-1);
        rc = EnvAddEnd2(fRM, sEnvVar, substr(sToAdd, j+1, i - j), sSeparator);
        i = j;
    end

return rc;


/**
 * Add sToAdd as the end of sEnvVar.
 *
 * Known features: Don't remove sToAdd from original value if sToAdd
 *                 is at the end and don't end with a ';'.
 */
EnvAddEnd2: procedure
    parse arg fRM, sEnvVar, sToAdd, sSeparator

    /* sets default separator if not specified. */
    if (sSeparator = '') then sSeparator = ';';

    /* checks that sToAdd ends with a separator. Adds one if not. */
    if (substr(sToAdd, length(sToAdd), 1) <> sSeparator) then
        sToAdd = sToAdd || sSeparator;

    /* check and evt. remove separator at start of sToAdd */
    if (substr(sToAdd, 1, 1) = sSeparator) then
        sToAdd = substr(sToAdd, 2);

    /* Get original variable value */
    sOrgEnvVar = EnvGet(sEnvVar);

    if (sOrgEnvVar <> '') then
    do
        /* Remove previously sToAdd if exists. (Changing sOrgEnvVar). */
        i = pos(translate(sToAdd), translate(sOrgEnvVar));
        if (i > 0) then
            sOrgEnvVar = substr(sOrgEnvVar, 1, i-1) || substr(sOrgEnvVar, i + length(sToAdd));

        /* checks that sOrgEnvVar ends with a separator. Adds one if not. */
        if (sOrgEnvVar = '') then
            if (right(sOrgEnvVar,1) <> sSeparator) then
                sOrgEnvVar = sOrgEnvVar || sSeparator;
    end

    /* set environment */
    if (fRM) then return EnvSet(0, sEnvVar, sOrgEnvVar);
return EnvSet(0, sEnvVar, sOrgEnvVar||sToAdd);


/**
 * Sets sEnvVar to sValue.
 */
EnvSet: procedure
    parse arg fRM, sEnvVar, sValue

    /* if we're to remove this, make valuestring empty! */
    if (fRM) then
        sValue = '';
    sEnvVar = translate(sEnvVar);

    /*
     * Begin/EndLibpath fix:
     *      We'll have to set internal these using both commandline 'SET'
     *      and internal VALUE in order to export it and to be able to
     *      get it (with EnvGet) again.
     */
    if ((sEnvVar = 'BEGINLIBPATH') | (sEnvVar = 'ENDLIBPATH')) then
    do
        if (length(sValue) >= 1024) then
            say 'Warning: 'sEnvVar' is too long,' length(sValue)' char.';
        return SysSetExtLibPath(sValue, substr(sEnvVar, 1, 1));
    end

    if (length(sValue) >= 1024) then
    do
        say 'Warning: 'sEnvVar' is too long,' length(sValue)' char.';
        say '    This may make CMD.EXE unstable after a SET operation to print the environment.';
    end
    sRc = VALUE(sEnvVar, sValue, 'OS2ENVIRONMENT');
return 0;


/**
 * Gets the value of sEnvVar.
 */
EnvGet: procedure
    parse arg sEnvVar
    if ((translate(sEnvVar) = 'BEGINLIBPATH') | (translate(sEnvVar) = 'ENDLIBPATH')) then
        return SysQueryExtLibPath(substr(sEnvVar, 1, 1));
return value(sEnvVar,, 'OS2ENVIRONMENT');


/**
 * Checks if a file exists.
 * @param   sFile       Name of the file to look for.
 * @param   sComplain   Complaint text. Complain if non empty and not found.
 * @returns TRUE if file exists.
 *          FALSE if file doesn't exists.
 */
FileExists: procedure
    parse arg sFile, sComplain
    rc = stream(sFile, 'c', 'query exist');
    if ((rc = '') & (sComplain <> '')) then
        say sComplain ''''sFile'''.';
return rc <> '';


/**
 * Checks if a directory exists.
 * @param   sDir        Name of the directory to look for.
 * @param   sComplain   Complaint text. Complain if non empty and not found.
 * @returns TRUE if file exists.
 *          FALSE if file doesn't exists.
 */
DirExists: procedure
    parse arg sDir, sComplain
    rc = SysFileTree(sDir, 'sDirs', 'DO');
    if (rc = 0 & sDirs.0 = 1) then
        return 1;
    if (sComplain <> '') then
        say sComplain ''''sDir'''.';
return 0;



/*
 * EMX/GCC 3.x.x - this environment must be used 'on' the ordinary EMX.
 * Note! bin.new has been renamed to bin!
 * Note! make .lib of every .a! in 4OS2: for /R %i in (*.a) do if not exist %@NAME[%i].lib emxomf %i
 */
GCC3xx: procedure expose aCfg. aPath. sPathFile sPathTools sPathToolsF
    parse arg sToolId,sOperation,fRM,fQuiet,sPathId

    /*
     * EMX/GCC main directory.
     */
    /*sGCC = PathQuery(sPathId, sToolId, sOperation);
    if (sGCC = '') then
        return 1;
    /* If config operation we're done now. */
    if (pos('config', sOperation) > 0) then
        return 0;
    */
    sGCC = sPathTools'\x86.os2\gcc\staged'
    sGCCBack    = translate(sGCC, '\', '/');
    sGCCForw    = translate(sGCC, '/', '\');
    chMajor     = '3';
    chMinor     = left(right(sToolId, 2), 1);
    chRel       = right(sToolId, 1);
    sVer        = chMajor'.'chMinor'.'chRel

    call EnvSet      fRM, 'PATH_IGCC',      sGCCBack;
    call EnvSet      fRM, 'CCENV',          'IGCC'
    call EnvSet      fRM, 'BUILD_ENV',      'IGCC'
    call EnvSet      fRM, 'BUILD_PLATFORM', 'OS2'

    call EnvAddFront fRM, 'BEGINLIBPATH',       sGCCBack'\lib;'
    call EnvAddFront fRM, 'PATH',               sGCCForw'/bin;'sGCCBack'\bin;'
    call EnvAddFront fRM, 'C_INCLUDE_PATH',     sGCCForw'/include'
    call EnvAddFront fRM, 'LIBRARY_PATH',       sGCCForw'/lib/gcc-lib/i386-pc-os2-emx/'sVer';'sGCCForw'/lib;'sGCCForw'/lib/mt;'
    call EnvAddFront fRm, 'CPLUS_INCLUDE_PATH', sGCCForw'/include;'
    call EnvAddFront fRm, 'CPLUS_INCLUDE_PATH', sGCCForw'/include/c++/'sVer'/backward;'
    call EnvAddFront fRm, 'CPLUS_INCLUDE_PATH', sGCCForw'/include/c++/'sVer'/i386-pc-os2-emx;'
    call EnvAddFront fRm, 'CPLUS_INCLUDE_PATH', sGCCForw'/include/c++/'sVer';'
    call EnvSet      fRM, 'PROTODIR',           sGCCForw'/include/cpp/gen'
    call EnvSet      fRM, 'OBJC_INCLUDE_PATH',  sGCCForw'/include'
    call EnvAddFront fRM, 'INFOPATH',           sGCCForw'/info'
    call EnvSet      fRM, 'EMXBOOK',            'emxdev.inf+emxlib.inf+emxgnu.inf+emxbsd.inf'
    call EnvAddFront fRM, 'HELPNDX',            'emxbook.ndx', '+', 1

    /*
     * Verify.
     */
    if (pos('verify', sOperation) <= 0) then
        return 0;

    if (rc = 0) then
        rc = CheckCmdOutput('g++ --version', 0, fQuiet, sVer);
    if (rc = 0) then
    do
        sVerAS = '2.14';
        rc = CheckCmdOutput('as --version', 0, fQuiet, 'GNU assembler 'sVerAS);
    end
return rc;



/**
 *  Workaround for bug in CMD.EXE.
 *  It messes up when REXX have expanded the environment.
 */
FixCMDEnv: procedure
/* do this anyway
    /* check for 4OS2 first */
    Address CMD 'set 4os2test_env=%@eval[2 + 2]';
    if (value('4os2test_env',, 'OS2ENVIRONMENT') = '4') then
        return 0;
*/

    /* force environment expansion by setting a lot of variables and freeing them.
     * ~6600 (bytes) */
    do i = 1 to 100
        Address CMD '@set dummyenvvar'||i'=abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ';
    end
    do i = 1 to 100
        Address CMD '@set dummyenvvar'||i'=';
    end
return 0;


/**
 * Translate a string to lower case.
 */
ToLower: procedure
    parse arg sString
return translate(sString, 'abcdefghijklmnopqrstuvwxyz', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ');


/**
 * Gets the script directory.
 */
GetScriptDir: procedure
    /*
     * Assuming this script is in the root directory, we can determing
     * the abs path to it by using the 'parse source' feature in rexx.
     */
    parse source . . sScript
    sScriptDir = filespec('drive', sScript) || strip(filespec('path', sScript), 'T', '\');
return sScriptDir;

