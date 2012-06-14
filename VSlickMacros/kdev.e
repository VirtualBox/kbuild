/* $Id: kdev.e 2587 2012-06-14 15:15:49Z knut.osmundsen@oracle.com $  -*- tab-width: 4 c-indent-level: 4 -*- */
/** @file
 * Visual SlickEdit Documentation Macros.
 */

/*
 * Copyright (c) 1999-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

/***
 *
 * This define the following keys:
 *---------------------------------
 * Ctrl+Shift+C: Class description box.
 * Ctrl+Shift+F: Function/method description box.
 * Ctrl+Shift+M: Module(file) description box
 * Ctrl+Shift+O: One-liner (comment)
 *
 * Ctrl+Shift+G: Global box
 * Ctrl+Shift+H: Header box
 * Ctrl+Shift+E: Exported Symbols
 * Ctrl+Shift+I: Internal function box
 * Ctrl+Shift+K: Const/macro box
 * Ctrl+Shift+S: Struct/Typedef box
 *
 * Ctrl+Shift+A: Signature+Date marker
 * Ctrl+Shift+P: Mark line as change by me
 *
 * Ctrl+Shift+T: Update project tagfile.
 * Ctrl+Shift+L: Load document variables.
 *
 * Ctrl+Shift+B: KLOGENTRYX(..)
 * Ctrl+Shift+E: KLOGEXIT(..)
 * Ctrl+Shift+N: Do kLog stuff for the current file. No questions.
 * Ctrl+Shift+Q: Do kLog stuff for the current file. Ask a lot of questions.
 *
 * Remember to set the correct sOdin32UserName, sOdin32UserEmail and sOdin32UserInitials
 * before compiling and loading the macros into Visual SlickEdit.
 *
 * These macros are compatible with both 3.0(c) and 4.0(b).
 *
 */
defeventtab default_keys
def  'C-S-A' = k_signature
def  'C-S-C' = k_javadoc_classbox
def  'C-S-E' = k_box_exported
def  'C-S-F' = k_javadoc_funcbox
def  'C-S-G' = k_box_globals
def  'C-S-H' = k_box_headers
def  'C-S-I' = k_box_intfuncs
def  'C-S-K' = k_box_consts
def  'C-S-M' = k_javadoc_moduleheader
def  'C-S-O' = k_oneliner
def  'C-S-P' = k_mark_modified_line
def  'C-S-S' = k_box_structs
def  'C-S-T' = k_rebuild_tagfile
def  'C-S-L' = k_style_load

//optional stuff
//def  'C-S-Q' = klib_klog_file_ask
//def  'C-S-N' = klib_klog_file_no_ask
//def  'C-S-1' = klib_klogentry
//def  'C-S-3' = klib_klogexit


//MARKER.  Editor searches for this line!
#pragma option(redeclvars, on)
#include 'slick.sh'
#ifndef VS_TAGDETAIL_context_args
/* newer vslick version. */
#include 'tagsdb.sh'
//#pragma option(strict,on)
/*#else: Version 4.0 (OS/2) */
#endif

/* Remeber to change these! */
static _str skUserInitials  = "bird";
static _str skUserName      = "knut st. osmundsen";
static _str skUserEmail     = "bird-kBuild-spamx@anduin.net";


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
static _str     skCodeStyle     = 'Opt2Ind4'; /* coding style scheme. */
static _str     skDocStyle      = 'javadoc';/* options: javadoc, */
static _str     skLicense       = 'GPLv3';  /* options: GPL, LGPL, Odin32, Confidential, ++ */
static _str     skCompany       = '';       /* empty or company name for copyright */
static _str     skProgram       = '';       /* Current program name - used by [L]GPL */
static _str     skChange        = '';       /* Current change identifier. */

static int      ikStyleWidth    = 80;       /* The page width of the style. */
static boolean  fkStyleFullHeaders = false; /* false: omit some tags. */
static int      ikStyleOneliner = 41;       /* The oneline comment column. */
static int      ikStyleModifyMarkColumn = 105;
static boolean  fkStyleBoxTag   = false;    /* true: Include tag in k_box_start. */

/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
/**
 * Gets iso date.
 * @returns ISO formatted date.
 */
static _str k_date()
{
    int i,j;
    _str date;

    date = _date('U');
    i = pos("/", date);
    j = pos("/", date, i+1);
    _str month = substr(date, 1, i-1);
    if (length(month) == 1) month = '0'month;
    _str day   = substr(date, i+1, j-i-1);
    if (length(day)   == 1) day   = '0'day;
    _str year  = substr(date, j+1);
    return year"-"month"-"day;
}


/**
 * Get the current year.
 * @returns   Current year string.
 */
static _str k_year()
{
    _str date = _date('U');
    return  substr(date, pos("/",date, pos("/",date)+1)+1, 4);
}


/**
 * Aligns a value up to a given alignment.
 */
static int k_alignup(int iValue, iAlign)
{
    if (iAlign <= 0)
    {
        message('k_alignup: iValue='iValue ' iAlign='iAlign);
        iAlign = 4;
    }
    return ((iValue intdiv iAlign) + 1) * iAlign;
}


/**
 * Reads the comment setup for this lexer/extension .
 *
 * @returns Success indicator.
 * @param   sLeft       Left comment. (output)
 * @param   sRight      Right comment. (output)
 * @param   iColumn     Comment mark column. (1-based) (output)
 * @param   sExt        The extension to lookup defaults to the current one.
 * @param   sLexer      The lexer to lookup defaults to the current one.
 * @remark  This should be exported from box.e, but unfortunately it isn't.
 */
static boolean k_commentconfig(_str &sLeft, _str &sRight, int &iColumn, _str sExt = p_extension, _str sLexer = p_lexer_name)
{
    /* init returns */
    sLeft = sRight = '';
    iColumn = 0;

    /*
     * Get comment setup from the lexer.
     */
    _str sLine = '';
    if (sLexer)
    {
        /* multiline */
#if __VERSION__>=14.0
        _str aComments[];
        GetComments(aComments, "mlcomment", sLexer)
        for (i = 0; i < aComments._length(); i++)
            if (!pos("documentation", aComments[i]) > 0)
            {
                sLine = aComments[i];
                break;
            }
        if (sLine != '')
#else
        rc = _ini_get_value(slick_path_search("user.vlx"), sLexer, 'mlcomment', sLine);
        if (rc)
            rc = _ini_get_value(slick_path_search("vslick.vlx"), sLexer, 'mlcomment', sLine);
        if (!rc)
#endif
        {
            sLeft  = strip(word(sLine, 1));
            sRight = strip(word(sLine, 2));
            if (sLeft != '' && sRight != '')
                return true;
        }

        /* failed, try single line. */
#if __VERSION__>=14.0
        GetComments(aComments, "linecomment", sLexer)
        for (i = 0; i < aComments._length(); i++)
            if (!pos("documentation", aComments[i]) > 0)
            {
                sLine = aComments[i];
                break;
            }
        if (sLine != '')
#else
        rc = _ini_get_value(slick_path_search("user.vlx"), sLexer, 'linecomment', sLine);
        if (rc)
            rc = _ini_get_value(slick_path_search("vslick.vlx"), sLexer, 'linecomment', sLine);
        if (!rc)
#endif
        {
            sLeft = strip(word(sLine, 1));
            sRight = '';
            iColumn = 0;
            _str sTmp = word(sLine, 2);
            if (isnumber(sTmp))
                iColumn = (int)sTmp;
            if (sLeft != '')
                return true;
        }
    }

    /*
     * Read the nonboxchars and determin user or default box.ini.
     */
    _str sFile = slick_path_search("ubox.ini");
    boolean frc = _ini_get_value(sFile, sExt, 'nonboxchars', sLine);
    if (frc)
    {
        sFile = slick_path_search("box.ini");
        frc = _ini_get_value(sFile, sExt, 'nonboxchars', sLine);
    }

    if (!frc)
    {   /*
         * Found extension.
         */
        sLeft = strip(eq_name2value('left',sLine));
        if (sLeft  == '\e') sLeft = '';
        sRight = strip(eq_name2value('right',sLine));
        if (sRight == '\e') sRight = '';

        /* Read comment column too */
        frc = _ini_get_value(sFile, sExt, 'comment_col', sLine);
        if (frc)
        {
            iColumn = eq_name2value('comment_col', sLine);
            if (iColumn == '\e') iColumn = 0;
        }
        else
            iColumn = 0;
        return true;
    }

    /* failure */
    sLeft = sRight = '';
    iColumn = 0;

    return false;
}


/**
 * Checks if current file only support line comments.
 * @returns True / False.
 * @remark  Use builtin extension stuff!
 */
static boolean k_line_comment()
{
    _str    sRight = '';
    _str    sLeft = '';
    int     iColumn;
    boolean fLineComment = false;
    if (k_commentconfig(sLeft, sRight, iColumn))
        fLineComment = (sRight == '' || iColumn > 0);
    return fLineComment;
}



#define KIC_CURSOR_BEFORE 1
#define KIC_CURSOR_AFTER  2
#define KIC_CURSOR_AT_END 3

/**
 * Insert a comment at current or best fitting position in the text.
 * @param   sStr            The comment to insert.
 * @param   iCursor         Where to put the cursor.
 * @param   iPosition       Where to start the comment.
 *                          Doesn't apply to column based source.
 *                          -1 means at cursor position. (default)
 *                          >0 means at end of line, but not before this column (1-based).
 *                             This also implies a min of one space to EOL.
 */
void k_insert_comment(_str sStr, int iCursor, int iPosition = -1)
{
    _str    sLeft;
    _str    sRight;
    int     iColumn;
    if (!k_commentconfig(sLeft, sRight, iColumn))
    {
        sLeft = '/*'; sRight = '*/'; iColumn = 0;
    }

    int iCol = 0;
    if (iColumn <= 0)
    {   /*
         * not column based source
         */

        /* position us first */
        if (iPosition > 0)
        {
            end_line();
            do {
                _insert_text(" ");
            } while (p_col < iPosition);
        }

        /* insert comment saving the position for _BEFORE. */
        iCol = p_col;
        _insert_text(sLeft:+' ':+sStr);
        if (iCursor == KIC_CURSOR_AT_END)
            iCol = p_col;
        /* right comment delimiter? */
        if (sRight != '')
            _insert_text(' ':+sRight);
    }
    else
    {
        if (p_col >= iColumn)
            _insert_text("\n");
        do { _insert_text(" "); } while (p_col < iColumn);
        if (iCursor == KIC_CURSOR_BEFORE)
            iCol = p_col;
        _insert_text(sLeft:+' ':+sStr);
        if (iCursor == KIC_CURSOR_AT_END)
            iCol = p_col;
    }

    /* set cursor. */
    if (iCursor != KIC_CURSOR_AFTER)
        p_col = iCol;
}


/**
 * Gets the comment prefix or postfix.
 * @returns Comment prefix or postfix.
 * @param   fRight  If clear left comment string - default.
 *                  If set right comment string.
 */
static _str k_comment(boolean fRight = false)
{
    _str sLeft, sRight;
    int iColumn;
    _str sComment = '/*';
    if (k_commentconfig(sLeft, sRight, iColumn))
        sComment = (!fRight || iColumn > 0 ? sLeft : sRight);

    return strip(sComment);
}


/*******************************************************************************
*   BOXES                                                                      *
*******************************************************************************/

/**
 * Inserts the first line in a box.
 * @param     sTag  Not used - box tag.
 */
static void k_box_start(sTag)
{
    _str sLeft, sRight;
    int iColumn;
    if (!k_commentconfig(sLeft, sRight, iColumn))
        return;
    _begin_line();
    if (iColumn >= 0)
        while (p_col < iColumn)
           _insert_text(" ");

    _str sText = sLeft;
    if (sTag != '' && fkStyleBoxTag)
    {
        if (substr(sText, length(sText)) != '*')
            sText = sText:+'*';
        sText = sText:+sTag;
    }

    int i;
    for (i = length(sText); i <= ikStyleWidth - p_col; i++)
        sText = sText:+'*';
    sText = sText:+"\n";

    _insert_text(sText);
}


/**
 * Places a string, sStr, into a line started and ended by '*'.
 * @param   sStr    Text to have between the '*'s.
 */
static void k_box_line(_str sStr)
{
    _str sLeft, sRight;
    int iColumn;
    if (!k_commentconfig(sLeft, sRight, iColumn))
        return;
    if (iColumn >= 0)
        while (p_col < iColumn)
           _insert_text(" ");

    _str sText = '';
    if (k_line_comment())
        sText = sLeft;
    if (sText == '' || substr(sText, length(sText)) != '*')
        sText = sText:+'*';

    sText = sText:+' ';
    int i;
    for (i = length(sText); i < p_SyntaxIndent; i++)
        sText = sText:+' ';

    sText = sText:+sStr;

    for (i = length(sText) + 1; i <= ikStyleWidth - p_col; i++)
        sText = sText:+' ';
    sText = sText:+"*\n";

    _insert_text(sText);
}


/**
 * Inserts the last line in a box.
 */
static void k_box_end()
{
    _str sLeft, sRight;
    int iColumn, i;
    if (!k_commentconfig(sLeft, sRight, iColumn))
        return;
    if (iColumn >= 0)
        while (p_col < iColumn)
           _insert_text(" ");

    _str sText = '';
    if (k_line_comment())
        sText = sLeft;
    for (i = length(sText) + length(sRight); i <= ikStyleWidth - p_col; i++)
        sText = sText:+'*';
    sText = sText:+sRight:+"\n";

    _insert_text(sText);
}



/*******************************************************************************
*   FUNCTION AND CODE PARSERS                                                  *
*******************************************************************************/
/**
 * Moves cursor to nearest function start.
 * @returns 0 if ok.
 *          -1 on failure.
 */
static int k_func_goto_nearest_function()
{
    boolean fFix = false;               /* cursor at function fix. (last function) */
    int cur_line = p_line;
    int prev_line = -1;
    int next_line = -1;
    typeless org_pos;
    _save_pos2(org_pos);

    if (!next_proc(1))
    {
        next_line = p_line;
        if (!prev_proc(1) && p_line == cur_line)
        {
            _restore_pos2(org_pos);
            return 0;
        }
        _restore_pos2(org_pos);
        _save_pos2(org_pos);
    }
    else
    {
        p_col++;                        /* fixes problem with single function files. */
        fFix = true;
    }

    if (!prev_proc(1))
    {
        prev_line = p_line;
        if (!next_proc(1) && p_line == cur_line)
        {
            _restore_pos2(org_pos);
            return 0;
        }
        _restore_pos2(org_pos);
        _save_pos2(org_pos);
    }


    if (prev_line != -1 && (next_line == -1 || cur_line - prev_line <= next_line - cur_line))
    {
        if (fFix)
            p_col++;
        prev_proc(1);
        return 0;
    }

    if (next_line != -1 && (prev_line == -1 || cur_line - prev_line > next_line - cur_line))
    {
        next_proc();
        return 0;
    }

    _restore_pos2(org_pos);
    return -1;
}


/**
 * Check if nearest function is a prototype.
 * @returns True if function prototype.
 *          False if not function prototype.
 */
static boolean k_func_prototype()
{
    /*
     * Check if this is a real function implementation.
     */
    typeless procpos;
    _save_pos2(procpos);
    if (!k_func_goto_nearest_function())
    {
        int proc_line = p_line;

        if (!k_func_searchcode("{"))
        {
            prev_proc();
            if (p_line != proc_line)
            {
                _restore_pos2(procpos);
                return true;
            }
        }
    }
    _restore_pos2(procpos);

    return false;
}


/**
 * Gets the name fo the current function.
 * @returns The current function name.
 */
static _str k_func_getfunction_name()
{
    _str sFunctionName = current_proc();
    if (!sFunctionName)
        sFunctionName = "";
    //say 'functionanme='sFunctionName;
    return sFunctionName;
}


/**
 * Goes to the neares function and gets its parameters.
 * @remark  Should be reimplemented to use tags (if someone can figure out how to query that stuff).
 */
static _str k_func_getparams()
{
    typeless org_pos;
    _save_pos2(org_pos);

    /*
     * Try use the tags first.
     */
    _UpdateContext(true);
    int context_id = tag_current_context();
    if (context_id <= 0)
    {
        k_func_goto_nearest_function();
        context_id = tag_current_context();
    }
    if (context_id > 0)
    {
        _str args = '';
        _str type = '';
       tag_get_detail2(VS_TAGDETAIL_context_args, context_id, args);
       tag_get_detail2(VS_TAGDETAIL_context_type, context_id, type);
       if (tag_tree_type_is_func(type))
           return args
           //caption = tag_tree_make_caption_fast(VS_TAGMATCH_context,context_id,true,true,false);
    }

    /*
     * Go to nearest function.
     */
    if (    !k_func_goto_nearest_function()
        &&  !k_func_searchcode("(")     /* makes some assumptions. */
        )
    {
        /*
         * Get parameters.
         */
        typeless posStart;
        _save_pos2(posStart);
        long offStart = _QROffset();
        if (!find_matching_paren())
        {
            long offEnd = _QROffset();
            _restore_pos2(posStart);
            p_col++;
            _str sParamsRaw = strip(get_text((int)(offEnd - offStart - 1)));


            /*
             * Remove new lines and double spaces within params.
             */
            _str sParams = "";

            int i;
            _str chPrev;
            for (i = 1, chPrev = ' '; i <= length(sParamsRaw); i++)
            {
                _str ch = substr(sParamsRaw, i, 1);

                /*
                 * Do fixups.
                 */
                if (ch == " " && chPrev == " ")
                        continue;

                if ((ch :== "\n") || (ch :== "\r") || (ch :== "\t"))
                {
                    if (chPrev == ' ')
                        continue;
                    ch = ' ';
                }

                if (ch == ',' && chPrev == ' ')
                {
                    sParams = substr(sParams, 1, length(sParams) - 1);
                }

                if (ch == '*')
                {
                    if (chPrev != ' ')
                        sParams = sParams :+ ' * ';
                    else
                        sParams = sParams :+ '* ';
                    chPrev = ' ';
                }
                else
                {
                    sParams = sParams :+ ch;
                    chPrev = ch;
                }

            } /* for */

            sParams = strip(sParams);
            if (sParams == 'void' || sParams == 'VOID')
                sParams = "";
            _restore_pos2(org_pos);
            return sParams;
        }
        else
            message("find_matchin_paren failed");
    }

    _restore_pos2(org_pos);
    return false;
}



/**
 * Enumerates the parameters to the function.
 * @param   sParams     Parameter string from k_func_getparams.
 * @param   iParam      The index (0-based) of the parameter to get.
 * @param   sType       Type. (output)
 * @param   sName       Name. (output)
 * @param   sDefault    Default value. (output)
 * @remark  Doesn't perhaps handle function pointers very well (I think)?
 * @remark  Should be reimplemented to use tags (if someone can figure out how to query that stuff).
 */
static int k_func_enumparams(_str sParams, int iParam, _str &sType, _str &sName, _str &sDefault)
{
    int     i;
    int     iParLevel;
    int     iCurParam;
    int     iStartParam;

    sType = sName = sDefault = "";

    /* no use working on empty string! */
    if (length(sParams) == 0)
        return -1;

    /* find the parameter in question */
    for (iStartParam = i = 1, iParLevel = iCurParam = 0; i <= length(sParams); i++)
    {
        _str ch = substr(sParams, i, 1);
        if (ch == ',' && iParLevel == 0)
        {
            /* is it this parameter ? */
            if (iParam == iCurParam)
                break;

            iCurParam++;
            iStartParam = i + 1;
        }
        else if (ch == '(')
            iParLevel++;
        else if (ch == ')')
            iParLevel--;
    }

    /* did we find the parameter? */
    if (iParam == iCurParam)
    {   /* (yeah, we did!) */
        _str sArg = strip(substr(sParams, iStartParam, i - iStartParam));
        /* remove M$ stuff */
        sArg = stranslate(sArg, "", "IN", "E");
        sArg = stranslate(sArg, "", "OUT", "E");
        sArg = stranslate(sArg, "", "OPTIONAL", "E");
        sArg = strip(sArg);

        /* lazy approach, which doens't support function types */

        if (pos('=', sParams) > 0)      /* default */
        {
            sDefault = strip(substr(sParams, pos('=', sParams) + 1));
            sArg = strip(substr(sArg, 1, pos('=', sParams) - 1));
        }

        for (i = length(sArg); i > 1; i--)
        {
            _str ch = substr(sArg, i, 1);
            if (    !(ch >= 'a' &&  ch <= 'z')
                &&  !(ch >= 'A' &&  ch <= 'Z')
                &&  !(ch >= '0'  && ch <= '9')
                &&  ch != '_' && ch != '$')
                break;
        }
        if (sArg == "...")
            i = 0;
        sName = strip(substr(sArg, i + 1));
        sType = strip(substr(sArg, 1, i));

        return 0;
    }

    return -1;
}


/**
 * Counts the parameters to the function.
 * @param   sParams     Parameter string from k_func_getparams.
 * @remark  Should be reimplemented to use tags (if someone can figure out how to query that stuff).
 */
static int k_func_countparams(_str sParams)
{
    int     i;
    int     iParLevel;
    int     iCurParam;
    _str    sType = "", sName = "", sDefault = "";

    /* check for 0 parameters */
    if (length(sParams) == 0)
        return 0;

    /* find the parameter in question */
    for (i = 1, iParLevel = iCurParam = 0; i <= length(sParams); i++)
    {
        _str ch = substr(sParams, i, 1);
        if (ch == ',' && iParLevel == 0)
        {
            iCurParam++;
        }
        else if (ch == '(')
            iParLevel++;
        else if (ch == ')')
            iParLevel--;
    }

    return iCurParam + 1;
}


/**
 * Gets the return type.
 */
static _str k_func_getreturntype(boolean fPureType = false)
{
    typeless org_pos;
    _save_pos2(org_pos);

    /*
     * Go to nearest function.
     */
    if (!k_func_goto_nearest_function())
    {
        /*
         * Return type is from function start to function name...
         */
        typeless posStart;
        _save_pos2(posStart);
        long offStart = _QROffset();

        if (!k_func_searchcode("("))               /* makes some assumptions. */
        {
            prev_word();
            long offEnd = _QROffset();
            _restore_pos2(posStart);
            _str sTypeRaw = strip(get_text((int)(offEnd - offStart)));

            //say 'sTypeRaw='sTypeRaw;
            /*
             * Remove static, inline, _Optlink, stdcall, EXPENTRY etc.
             */
            if (fPureType)
            {
                sTypeRaw = stranslate(sTypeRaw, "", "__static__", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__static", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "static__", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "static", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__inline__", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__inline", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "inline__", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "inline", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "EXPENTRY", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "_Optlink", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__stdcall", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__cdecl", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "_cdecl", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "cdecl", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__PASCAL", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "_PASCAL", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "PASCAL", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__Far32__", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__Far32", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "Far32__", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "_Far32_", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "_Far32", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "Far32_", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "Far32", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__far", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "_far", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "far", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__near", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "_near", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "near", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__loadds__", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__loadds", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "loadds__", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "_loadds_", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "_loadds", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "loadds_", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "loadds", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__loades__", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__loades", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "loades__", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "_loades_", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "_loades", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "loades_", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "loades", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "WIN32API", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "WINAPI", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "LDRCALL", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "KRNLCALL", "I");
                sTypeRaw = stranslate(sTypeRaw, "", "__operator__", "I"); /* operator fix */
                sTypeRaw = stranslate(sTypeRaw, "", "__operator", "I");   /* operator fix */
                sTypeRaw = stranslate(sTypeRaw, "", "operator__", "I");   /* operator fix */
                sTypeRaw = stranslate(sTypeRaw, "", "operator", "I");     /* operator fix */
                sTypeRaw = stranslate(sTypeRaw, "", "IN", "E");
                sTypeRaw = stranslate(sTypeRaw, "", "OUT", "E");
                sTypeRaw = stranslate(sTypeRaw, "", "OPTIONAL", "E");
            }

            /*
             * Remove new lines and double spaces within params.
             */
            _str sType = "";

            int i;
            _str chPrev;
            for (i = 1, chPrev = ' '; i <= length(sTypeRaw); i++)
            {
                _str ch = substr(sTypeRaw, i, 1);

                /*
                 * Do fixups.
                 */
                if (ch == " " && chPrev == " ")
                        continue;

                if ((ch :== "\n") || (ch :== "\r") || (ch :== "\t"))
                {
                    if (chPrev == ' ')
                        continue;
                    ch = ' ';
                }

                if (ch == ',' && chPrev == ' ')
                {
                    sType = substr(sType, 1, length(sType) - 1);
                }

                if (ch == '*')
                {
                    if (chPrev != ' ')
                        sType = sType :+ ' * ';
                    else
                        sType = sType :+ '* ';
                    chPrev = ' ';
                }
                else
                {
                    sType = sType :+ ch;
                    chPrev = ch;
                }

            } /* for */

            sType = strip(sType);

            _restore_pos2(org_pos);
            return sType;
        }
        else
            message('k_func_getreturntype: can''t find ''(''.');
    }

    _restore_pos2(org_pos);
    return false;
}


/**
 * Search for some piece of code.
 */
static int k_func_searchcode(_str sSearchString, _str sOptions = "E+")
{
    int rc;
    rc = search(sSearchString, sOptions);
    while (!rc && !k_func_in_code())
    {
        p_col++;
        rc = search(sSearchString, sOptions);
    }
    return rc;
}


/**
 * Checks if cursor is in code or in comment.
 * @return  True if cursor in code.
 */
static boolean k_func_in_code()
{
    typeless searchsave;
    _save_pos2(searchsave);
    boolean fRc = !_in_comment();
    _restore_pos2(searchsave);
    return fRc;
}


/*
 * Gets the next piece of code.
 */
static _str k_func_get_next_code_text()
{
    typeless searchsave;
    _save_pos2(searchsave);
    _str ch = k_func_get_next_code_text2();
    _restore_pos2(searchsave);
    return ch;
}


/**
 * Checks if there is more code on the line.
 */
static boolean k_func_more_code_on_line()
{
    boolean fRc;
    int     curline = p_line;
    typeless searchsave;
    _save_pos2(searchsave);
    k_func_get_next_code_text2();
    fRc = curline == p_line;
    _restore_pos2(searchsave);

    return fRc;
}


/**
 * Gets the next piece of code.
 * Doesn't preserver cursor position.
 */
static _str k_func_get_next_code_text2()
{
    _str ch;
    do
    {
        int curcol = ++p_col;
        end_line();
        if (p_col <= curcol)
        {
            p_line++;
            p_col = 1;
        }
        else
            p_col = curcol;

        ch = get_text();
        //say ch ' ('_asc(ch)')';
        while (ch == "#")                  /* preprocessor stuff */
        {
            p_col = 1;
            p_line++;
            ch = get_text();
            //say ch ' ('_asc(ch)')';
            continue;
        }
    } while (ch :== ' ' || ch :== "\t" || ch :== "\n" || ch :== "\r" || !k_func_in_code());

    return ch;
}




/*******************************************************************************
*   JAVA DOC STYLED WORKERS                                                    *
*******************************************************************************/

/** starts a javadoc documentation box. */
static void k_javadoc_box_start(_str sStr = '', boolean fDouble = true)
{
    _str sLeft, sRight;
    int iColumn;
    if (!k_commentconfig(sLeft, sRight, iColumn))
        return;
    _begin_line();
    if (iColumn >= 0)
        while (p_col < iColumn)
           _insert_text(" ");

    _str sText = sLeft;
    if (fDouble)
        sText = sLeft:+substr(sLeft, length(sLeft), 1);
    if (sStr != '')
        sText = sText:+' ':+sStr;
    sText = sText:+"\n";

    _insert_text(sText);
}

/** inserts a new line in a javadoc documentation box. */
static void k_javadoc_box_line(_str sStr = '', int iPadd = 0, _str sStr2 = '', int iPadd2 = 0, _str sStr3 = '')
{
    _str sLeft, sRight;
    int iColumn;
    if (!k_commentconfig(sLeft, sRight, iColumn))
        return;
    if (iColumn >= 0)
        while (p_col < iColumn)
           _insert_text(" ");

    _str sText;
    if (k_line_comment())
        sText = sLeft;
    else
    {
        sText = sLeft;
        sText = ' ':+substr(sLeft, length(sLeft));
    }

    if (sStr != '')
        sText = sText:+' ':+sStr;
    if (iPadd > 0)
    {
        int i;
        for (i = length(sText); i < iPadd; i++)
            sText = sText:+' ';

        if (sStr2 != '')
            sText = sText:+sStr2;

        if (iPadd2 > 0)
        {
            for (i = length(sText); i < iPadd2; i++)
                sText = sText:+' ';

            if (sStr3 != '')
                sText = sText:+sStr3;
        }
    }
    sText = sText:+"\n";

    _insert_text(sText);
}

/** ends a javadoc documentation box. */
static void k_javadoc_box_end()
{
    _str sLeft, sRight;
    int iColumn;
    if (!k_commentconfig(sLeft, sRight, iColumn))
        return;
    if (iColumn >= 0)
        while (p_col < iColumn)
           _insert_text(" ");

    _str sText;
    if (k_line_comment())
        sText = sLeft;
    else
    {
        sText = sRight;
        /*if (substr(sText, 1, 1) != '*')
            sText = '*':+sText;*/
        sText = ' ':+sText;
    }
    sText = sText:+"\n";

    _insert_text(sText);
}


/**
 * Write a Javadoc styled classbox.
 */
void k_javadoc_classbox()
{
    int     iCursorLine;
    int     iPadd = k_alignup(12, p_SyntaxIndent);

    k_javadoc_box_start();
    iCursorLine = p_RLine;
    k_javadoc_box_line(' ');

    if (fkStyleFullHeaders)
    {
        k_javadoc_box_line('@shortdesc', iPadd);
        k_javadoc_box_line('@dstruct', iPadd);
        k_javadoc_box_line('@version', iPadd);
        k_javadoc_box_line('@verdesc', iPadd);
    }
    k_javadoc_box_line('@author', iPadd, skUserName ' <' skUserEmail '>');
    k_javadoc_box_line('@approval', iPadd);
    k_javadoc_box_end();

    up(p_RLine - iCursorLine);
    end_line();
    keyin(' ');
}


/**
 * Javadoc - functionbox(/header).
 */
void k_javadoc_funcbox()
{
    int     cArgs = 1;
    _str    sArgs = "";
    int     iCursorLine;
    int     iPadd = k_alignup(11, p_SyntaxIndent);

    /* look for parameters */
    boolean fFoundFn = !k_func_goto_nearest_function();
    if (fFoundFn)
    {
        sArgs = k_func_getparams();
        cArgs = k_func_countparams(sArgs);
    }

    k_javadoc_box_start();
    iCursorLine = p_RLine;
    k_javadoc_box_line(' ');
    if (file_eq(p_extension, 'asm'))
        k_javadoc_box_line('@cproto', iPadd);
    k_javadoc_box_line('@returns', iPadd);
    if (fFoundFn)
    {
        /*
         * Determin parameter description indent.
         */
        int     iPadd2 = 0;
        int     i;
        for (i = 0; i < cArgs; i++)
        {
            _str sName, sType, sDefault;
            if (   !k_func_enumparams(sArgs, i, sType, sName, sDefault)
                && iPadd2 < length(sName))
                iPadd2 = length(sName);
        }
        iPadd2 = k_alignup((iPadd + iPadd2), p_SyntaxIndent);
        if (iPadd2 < 28)
            iPadd2 = k_alignup(28, p_SyntaxIndent);

        /*
         * Insert parameter.
         */
        for (i = 0; i < cArgs; i++)
        {
            _str sName, sType, sDefault;
            if (!k_func_enumparams(sArgs, i, sType, sName, sDefault))
            {
                _str sStr3 = '.';
                if (sDefault != "")
                    sStr3 = '(default='sDefault')';
                k_javadoc_box_line('@param', iPadd, sName, iPadd2, sStr3);
            }
            else
                k_javadoc_box_line('@param', iPadd);
        }
    }
    else
        k_javadoc_box_line('@param', iPadd);

    if (file_eq(p_extension, 'asm'))
        k_javadoc_box_line('@uses', iPadd);
    if (fkStyleFullHeaders)
    {
        k_javadoc_box_line('@equiv', iPadd);
        k_javadoc_box_line('@time', iPadd);
        k_javadoc_box_line('@sketch', iPadd);
        k_javadoc_box_line('@status', iPadd);
        k_javadoc_box_line('@author', iPadd, skUserName ' <' skUserEmail '>');
        k_javadoc_box_line('@remark', iPadd);
    }
    k_javadoc_box_end();

    up(p_RLine - iCursorLine);
    end_line();
    keyin(' ');
}


/**
 * Javadoc module header.
 */
void k_javadoc_moduleheader()
{
    int iCursorLine;
    int fSplit = 0;

    _insert_text("\n");
    up();
    _begin_line();
    k_insert_comment('$':+'I':+'d: $', KIC_CURSOR_AT_END, -1);
    _end_line();
    _insert_text("\n");

    k_javadoc_box_start('@file');
    fSplit = 1;
    iCursorLine = p_RLine;
    k_javadoc_box_line();
    k_javadoc_box_end();
    _insert_text("\n");
    _insert_text(k_comment() "\n");

    if (skLicense == 'Confidential')
    {
        k_javadoc_box_line(skCompany ' confidential');
        k_javadoc_box_line();
    }

    if (skCompany != '')
    {
        if (skLicense != 'Confidential')
            k_javadoc_box_line('Copyright (C) ' k_year() ' ' skCompany);
        else
        {
            k_javadoc_box_line('Copyright (c) ' k_year() ' ' skCompany);
            k_javadoc_box_line();
            k_javadoc_box_line('Author: ' skUserName' <' skUserEmail '>');
        }
    }
    else
        k_javadoc_box_line('Copyright (c) ' k_year() ' 'skUserName' <' skUserEmail '>');
    k_javadoc_box_line();
    _str sProg = skProgram;
    switch (skLicense)
    {
        case 'Odin32':
            k_javadoc_box_line('Project Odin Software License can be found in LICENSE.TXT.');
            break;

        case 'GPL':
            if (!fSplit)
                k_javadoc_box_line();
            if (sProg == '')
                sProg = 'This program';
            else
            {
                k_javadoc_box_line('This file is part of ' sProg '.');
                k_javadoc_box_line();
            }
            k_javadoc_box_line(sProg ' is free software; you can redistribute it and/or modify');
            k_javadoc_box_line('it under the terms of the GNU General Public License as published by');
            k_javadoc_box_line('the Free Software Foundation; either version 2 of the License, or');
            k_javadoc_box_line('(at your option) any later version.');
            k_javadoc_box_line();
            k_javadoc_box_line(sProg ' is distributed in the hope that it will be useful,');
            k_javadoc_box_line('but WITHOUT ANY WARRANTY; without even the implied warranty of');
            k_javadoc_box_line('MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the');
            k_javadoc_box_line('GNU General Public License for more details.');
            k_javadoc_box_line();
            k_javadoc_box_line('You should have received a copy of the GNU General Public License');
            k_javadoc_box_line('along with ' sProg '; if not, write to the Free Software');
            k_javadoc_box_line('Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA');
            break;

        case 'LGPL':
            if (!fSplit)
                k_javadoc_box_line();
            if (sProg == '')
                sProg = 'This library';
            else
            {
                k_javadoc_box_line('This file is part of ' sProg '.');
                k_javadoc_box_line();
            }
            k_javadoc_box_line(sProg ' is free software; you can redistribute it and/or');
            k_javadoc_box_line('modify it under the terms of the GNU Lesser General Public');
            k_javadoc_box_line('License as published by the Free Software Foundation; either');
            k_javadoc_box_line('version 2.1 of the License, or (at your option) any later version.');
            k_javadoc_box_line();
            k_javadoc_box_line(sProg ' is distributed in the hope that it will be useful,');
            k_javadoc_box_line('but WITHOUT ANY WARRANTY; without even the implied warranty of');
            k_javadoc_box_line('MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU');
            k_javadoc_box_line('Lesser General Public License for more details.');
            k_javadoc_box_line();
            k_javadoc_box_line('You should have received a copy of the GNU Lesser General Public');
            k_javadoc_box_line('License along with ' sProg '; if not, write to the Free Software');
            k_javadoc_box_line('Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA');
            break;

        case 'GPLv3':
            if (!fSplit)
                k_javadoc_box_line();
            if (sProg == '')
                sProg = 'This program';
            else
            {
                k_javadoc_box_line('This file is part of ' sProg '.');
                k_javadoc_box_line();
            }
            k_javadoc_box_line(sProg ' is free software; you can redistribute it and/or modify');
            k_javadoc_box_line('it under the terms of the GNU General Public License as published by');
            k_javadoc_box_line('the Free Software Foundation; either version 3 of the License, or');
            k_javadoc_box_line('(at your option) any later version.');
            k_javadoc_box_line();
            k_javadoc_box_line(sProg ' is distributed in the hope that it will be useful,');
            k_javadoc_box_line('but WITHOUT ANY WARRANTY; without even the implied warranty of');
            k_javadoc_box_line('MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the');
            k_javadoc_box_line('GNU General Public License for more details.');
            k_javadoc_box_line();
            k_javadoc_box_line('You should have received a copy of the GNU General Public License');
            k_javadoc_box_line('along with ' sProg '.  If not, see <http://www.gnu.org/licenses/>');
            break;

        case 'LGPLv3':
            if (!fSplit)
                k_javadoc_box_line();
            if (sProg == '')
                sProg = 'This program';
            else
            {
                k_javadoc_box_line('This file is part of ' sProg '.');
                k_javadoc_box_line();
            }
            k_javadoc_box_line(sProg ' is free software; you can redistribute it and/or');
            k_javadoc_box_line('modify it under the terms of the GNU Lesser General Public');
            k_javadoc_box_line('License as published by the Free Software Foundation; either');
            k_javadoc_box_line('version 3 of the License, or (at your option) any later version.');
            k_javadoc_box_line();
            k_javadoc_box_line(sProg ' is distributed in the hope that it will be useful,');
            k_javadoc_box_line('but WITHOUT ANY WARRANTY; without even the implied warranty of');
            k_javadoc_box_line('MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the');
            k_javadoc_box_line('GNU Lesser General Public License for more details.');
            k_javadoc_box_line();
            k_javadoc_box_line('You should have received a copy of the GNU Lesser General Public License');
            k_javadoc_box_line('along with ' sProg '.  If not, see <http://www.gnu.org/licenses/>');
            break;

        case 'Confidential':
            k_javadoc_box_line('All Rights Reserved');
            break;

        case 'ConfidentialNoAuthor':
            k_javadoc_box_line(skCompany ' confidential');
            k_javadoc_box_line('All Rights Reserved');
            break;

        case 'VirtualBox':
            k_javadoc_box_line('This file is part of VirtualBox Open Source Edition (OSE), as')
            k_javadoc_box_line('available from http://www.virtualbox.org. This file is free software;')
            k_javadoc_box_line('you can redistribute it and/or modify it under the terms of the GNU')
            k_javadoc_box_line('General Public License (GPL) as published by the Free Software')
            k_javadoc_box_line('Foundation, in version 2 as it comes in the "COPYING" file of the')
            k_javadoc_box_line('VirtualBox OSE distribution. VirtualBox OSE is distributed in the')
            k_javadoc_box_line('hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.')
            k_javadoc_box_line('')
            k_javadoc_box_line('Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa')
            k_javadoc_box_line('Clara, CA 95054 USA or visit http://www.sun.com if you need')
            k_javadoc_box_line('additional information or have any questions.')
            break;

        case 'VirtualBoxGPLAndCDDL':
            k_javadoc_box_line('This file is part of VirtualBox Open Source Edition (OSE), as')
            k_javadoc_box_line('available from http://www.virtualbox.org. This file is free software;')
            k_javadoc_box_line('you can redistribute it and/or modify it under the terms of the GNU')
            k_javadoc_box_line('General Public License (GPL) as published by the Free Software')
            k_javadoc_box_line('Foundation, in version 2 as it comes in the "COPYING" file of the')
            k_javadoc_box_line('VirtualBox OSE distribution. VirtualBox OSE is distributed in the')
            k_javadoc_box_line('hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.')
            k_javadoc_box_line('')
            k_javadoc_box_line('The contents of this file may alternatively be used under the terms')
            k_javadoc_box_line('of the Common Development and Distribution License Version 1.0')
            k_javadoc_box_line('(CDDL) only, as it comes in the "COPYING.CDDL" file of the')
            k_javadoc_box_line('VirtualBox OSE distribution, in which case the provisions of the')
            k_javadoc_box_line('CDDL are applicable instead of those of the GPL.')
            k_javadoc_box_line('')
            k_javadoc_box_line('You may elect to license modified versions of this file under the')
            k_javadoc_box_line('terms and conditions of either the GPL or the CDDL or both.')
            k_javadoc_box_line('')
            k_javadoc_box_line('Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa')
            k_javadoc_box_line('Clara, CA 95054 USA or visit http://www.sun.com if you need')
            k_javadoc_box_line('additional information or have any questions.')
            break;

        default:

    }
    k_javadoc_box_line();
    k_javadoc_box_end();

    up(p_RLine - iCursorLine);
    end_line();
    keyin(' ');
}







/*******************************************************************************
*   Keyboard Shortcuts                                                         *
*******************************************************************************/
/** Makes global box. */
void k_box_globals()
{
    k_box_start('Global');
    k_box_line('Global Variables');
    k_box_end();
}

/** Makes header box. */
void k_box_headers()
{
    k_box_start("Header");
    k_box_line("Header Files");
    k_box_end();
}

/** Makes internal function box. */
void k_box_intfuncs()
{
    k_box_start("IntFunc");
    k_box_line("Internal Functions");
    k_box_end();
}

/** Makes def/const box. */
void k_box_consts()
{
    k_box_start("Const");
    k_box_line("Defined Constants And Macros");
    k_box_end();
}

/** Structure box */
void k_box_structs()
{
    k_box_start("Struct");
    k_box_line("Structures and Typedefs");
    k_box_end();
}

/** Makes exported symbols box. */
void k_box_exported()
{
    k_box_start('Exported');
    k_box_line('Exported Symbols');
    k_box_end();
}



/** oneliner comment */
void k_oneliner()
{
    _str sLeft, sRight;
    int iColumn;
    if (    k_commentconfig(sLeft, sRight, iColumn)
        &&  iColumn > 0)
    {   /* column based needs some tricky repositioning. */
        _end_line();
        if (p_col > iColumn)
        {
            _begin_line();
            _insert_text("\n\r");
            up();
        }
    }
    k_insert_comment("", KIC_CURSOR_AT_END, ikStyleOneliner);
}

/** mark line as modified. */
void k_mark_modified_line()
{
    /* not supported for column based sources */
    _str sLeft, sRight;
    int iColumn;
    if (    !k_commentconfig(sLeft, sRight, iColumn)
        ||  iColumn > 0)
        return;
    _str sStr;
    if (skChange != '')
        sStr = skChange ' (' skUserInitials ')';
    else
        sStr = skUserInitials;
    k_insert_comment(sStr, KIC_CURSOR_BEFORE, ikStyleModifyMarkColumn);
    down();
}


/**
 * Inserts a signature. Form: "//Initials ISO-date:"
 * @remark    defeventtab
 */
void k_signature()
{
    /* kso I5-10000 2002-09-10: */
    _str sSig;
    if (skChange != '')
        sSig = skUserInitials ' ' skChange ' ' k_date() ': ';
    else
        sSig = skUserInitials ' ' k_date() ': ';
    k_insert_comment(sSig, KIC_CURSOR_AT_END);
}


/*******************************************************************************
*   kLIB Logging                                                               *
*******************************************************************************/
/**
 * Hot-Key: Inserts a KLOGENTRY statement at start of nearest function.
 */
void klib_klogentry()
{
    typeless org_pos;
    _save_pos2(org_pos);

    /*
     * Go to nearest function.
     */
    if (!k_func_goto_nearest_function())
    {
        /*
         * Get parameters.
         */
        _str sParams = k_func_getparams();
        if (sParams)
        {
            _str sRetType = k_func_getreturntype(true);
            if (!sRetType || sRetType == "")
                sRetType = "void";      /* paranoia! */

            /*
             * Insert text.
             */
            if (!k_func_searchcode("{"))
            {
                p_col++;
                int cArgs = k_func_countparams(sParams);
                if (cArgs > 0)
                {
                    _str sArgs = "";
                    int i;
                    for (i = 0; i < cArgs; i++)
                    {
                        _str sType, sName, sDefault;
                        if (!k_func_enumparams(sParams, i, sType, sName, sDefault))
                            sArgs = sArgs', 'sName;
                    }

                    _insert_text("\n    KLOGENTRY"cArgs"(\""sRetType"\",\""sParams"\""sArgs");"); /* todo tab size.. or smart indent */
                }
                else
                    _insert_text("\n    KLOGENTRY0(\""sRetType"\");"); /* todo tab size.. or smart indent */

                /*
                 * Check if the next word is KLOGENTRY.
                 */
                next_word();
                if (def_next_word_style == 'E')
                    prev_word();
                int iIgnorePos = 0;
                if (substr(cur_word(iIgnorePos), 1, 9) == "KLOGENTRY")
                    delete_line();

            }
            else
                message("didn't find {");
        }
        else
            message("k_func_getparams failed, sParams=" sParams);
        return;
    }

    _restore_pos2(org_pos);
}


/**
 * Hot-Key: Inserts a KLOGEXIT statement at cursor location.
 */
void klib_klogexit()
{
    typeless org_pos;
    _save_pos2(org_pos);

    /*
     * Go to nearest function.
     */
    if (!prev_proc())
    {
        /*
         * Get parameters.
         */
        _str sType = k_func_getreturntype(true);
        _restore_pos2(org_pos);
        if (sType)
        {
            boolean fReturn = true;     /* true if an return statment is following the KLOGEXIT statement. */

            /*
             * Insert text.
             */
            int cur_col = p_col;
            if (sType == 'void' || sType == 'VOID')
            {   /* procedure */
                int iIgnorePos;
                fReturn = cur_word(iIgnorePos) == 'return';
                if (!fReturn)
                {
                    while (p_col <= p_SyntaxIndent)
                        keyin(" ");
                }

                _insert_text("KLOGEXITVOID();\n");

                if (fReturn)
                {
                    int i;
                    for (i = 1; i < cur_col; i++)
                        _insert_text(" ");
                }
                search(")","E-");
            }
            else
            {   /* function */
                _insert_text("KLOGEXIT();\n");
                int i;
                for (i = 1; i < cur_col; i++)
                    _insert_text(" ");
                search(")","E-");

                /*
                 * Insert value if possible.
                 */
                typeless valuepos;
                _save_pos2(valuepos);
                next_word();
                if (def_next_word_style == 'E')
                    prev_word();
                int iIgnorePos;
                if (cur_word(iIgnorePos) == 'return')
                {
                    p_col += length('return');
                    typeless posStart;
                    _save_pos2(posStart);
                    long offStart = _QROffset();
                    if (!k_func_searchcode(";", "E+"))
                    {
                        long offEnd = _QROffset();
                        _restore_pos2(posStart);
                        _str sValue = strip(get_text((int)(offEnd - offStart)));
                        //say 'sValue = 'sValue;
                        _restore_pos2(valuepos);
                        _save_pos2(valuepos);
                        _insert_text(sValue);
                    }
                }
                _restore_pos2(valuepos);
            }

            /*
             * Remove old KLOGEXIT statement on previous line if any.
             */
            typeless valuepos;
            _save_pos2(valuepos);
            int newexitline = p_line;
            p_line--; p_col = 1;
            next_word();
            if (def_next_word_style == 'E')
                prev_word();
            int iIgnorePos;
            if (p_line == newexitline - 1 && substr(cur_word(iIgnorePos), 1, 8) == 'KLOGEXIT')
                delete_line();
            _restore_pos2(valuepos);

            /*
             * Check for missing '{...}'.
             */
            if (fReturn)
            {
                boolean fFound = false;
                _save_pos2(valuepos);
                p_col--; find_matching_paren(); p_col += 2;
                k_func_searchcode(';', 'E+'); /* places us at the ';' of the return. (hopefully) */

                _str ch = k_func_get_next_code_text();
                if (ch != '}')
                {
                    _restore_pos2(valuepos);
                    _save_pos2(valuepos);
                    p_col--; find_matching_paren(); p_col += 2;
                    k_func_searchcode(';', 'E+'); /* places us at the ';' of the return. (hopefully) */
                    p_col++;
                    if (k_func_more_code_on_line())
                        _insert_text(' }');
                    else
                    {
                        typeless returnget;
                        _save_pos2(returnget);
                        k_func_searchcode("return", "E-");
                        int return_col = p_col;
                        _restore_pos2(returnget);

                        end_line();
                        _insert_text("\n");
                        while (p_col < return_col - p_SyntaxIndent)
                            _insert_text(' ');
                        _insert_text('}');
                    }

                    _restore_pos2(valuepos);
                    _save_pos2(valuepos);
                    prev_word();
                    p_col -= p_SyntaxIndent;
                    int codecol = p_col;
                    _insert_text("{\n");
                    while (p_col < codecol)
                        _insert_text(' ');
                }

                _restore_pos2(valuepos);
            }
        }
        else
            message("k_func_getreturntype failed, sType=" sType);
        return;
    }

    _restore_pos2(org_pos);
}


/**
 * Processes a file - ask user all the time.
 */
void klib_klog_file_ask()
{
    klib_klog_file_int(true);
}


/**
 * Processes a file - no questions.
 */
void klib_klog_file_no_ask()
{
    klib_klog_file_int(false);
}



/**
 * Processes a file.
 */
static void klib_klog_file_int(boolean fAsk)
{
    show_all();
    bottom();
    _refresh_scroll();

    /* ask question so we can get to the right position somehow.. */
    if (fAsk && _message_box("kLog process this file?", "Visual SlickEdit", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    /*
     * Entries.
     */
    while (!prev_proc())
    {
        //say 'entry main loop: ' k_func_getfunction_name();

        /*
         * Skip prototypes.
         */
        if (k_func_prototype())
            continue;

        /*
         * Ask user.
         */
        center_line();
        _refresh_scroll();
        _str sFunction = k_func_getfunction_name();
        rc = fAsk ? _message_box("Process this function ("sFunction")?", "Visual SlickEdit", MB_YESNOCANCEL | MB_ICONQUESTION) : IDYES;
        if (rc == IDYES)
        {
            typeless procpos;
            _save_pos2(procpos);
            klib_klogentry();
            _restore_pos2(procpos);
        }
        else if (rc == IDNO)
            continue;
        else
            break;
    }

    /*
     * Exits.
     */
    bottom(); _refresh_scroll();
    boolean fUserCancel = false;
    while (!prev_proc() && !fUserCancel)
    {
        typeless procpos;
        _save_pos2(procpos);
        _str sCurFunction = k_func_getfunction_name();
        //say 'exit main loop: ' sCurFunction

        /*
         * Skip prototypes.
         */
        if (k_func_prototype())
            continue;

        /*
         * Select procedure.
         */
        while (   !k_func_searchcode("return", "WE<+")
               &&  k_func_getfunction_name() == sCurFunction)
        {
            //say 'exit sub loop: ' p_line
            /*
             * Ask User.
             */
            center_line();
            _refresh_scroll();
            _str sFunction = k_func_getfunction_name();
            rc =  fAsk ? _message_box("Process this exit from "sFunction"?", "Visual SlickEdit", MB_YESNOCANCEL | MB_ICONQUESTION) : IDYES;
            deselect();
            if (rc == IDYES)
            {
                typeless returnpos;
                _save_pos2(returnpos);
                klib_klogexit();
                _restore_pos2(returnpos);
                p_line++;
            }
            else if (rc != IDNO)
            {
                fUserCancel = true;
                break;
            }
            p_line++;                       /* just so we won't hit it again. */
        }

        /*
         * If void function we'll have to check if there is and return; prior to the ending '}'.
         */
        _restore_pos2(procpos);
        _save_pos2(procpos);
        _str sType = k_func_getreturntype(true);
        if (!fUserCancel && sType && (sType == 'void' || sType == 'VOID'))
        {
            if (    !k_func_searchcode("{", "E+")
                &&  !find_matching_paren())
            {
                typeless funcend;
                _save_pos2(funcend);
                prev_word();
                int iIgnorePos;
                if (cur_word(iIgnorePos) != "return")
                {
                    /*
                     * Ask User.
                     */
                    _restore_pos2(funcend);
                    center_line();
                    _refresh_scroll();
                    _str sFunction = k_func_getfunction_name();
                    rc = fAsk ? _message_box("Process this exit from "sFunction"?", "Visual SlickEdit", MB_YESNOCANCEL | MB_ICONQUESTION) : IDYES;
                    deselect();
                    if (rc == IDYES)
                    {
                        typeless returnpos;
                        _save_pos2(returnpos);
                        klib_klogexit();
                        _restore_pos2(returnpos);
                    }
                }
            }
        }

        /*
         * Next proc.
         */
        _restore_pos2(procpos);
    }
}

/** @todo move to kkeys.e */
_command void k_rebuild_tagfile()
{
#if 1 /*__VERSION__ < 14.0*/
    if (file_match('-p 'maybe_quote_filename(strip_filename(_project_name,'e'):+TAG_FILE_EXT),1) != "")
        _project_update_files_retag(false, false, false, false);
    else
        _project_update_files_retag(true,  false, false, true);
#else
    _str sArgs = "-refs=on";
    if (file_match('-p 'maybe_quote_filename(strip_filename(_project_name,'e'):+TAG_FILE_EXT),1) != "")
        sArgs = sArgs :+ " -retag";
    sArgs = sArgs :+ " " :+ _workspace_filename;
    build_workspace_tagfiles(sArgs);
#endif
}


/*******************************************************************************
*   Styles                                                                     *
*******************************************************************************/
static _str StyleLanguages[] =
{
    "c",
    "e",
    "java"
};

struct StyleScheme
{
    _str name;
    _str settings[];
};

static StyleScheme StyleSchemes[] =
{
    {
        "Opt2Ind4",
        {
           "orig_tabsize=4",
           "syntax_indent=4",
           "tabsize=4",
           "align_on_equal=1",
           "pad_condition_state=1",
           "indent_with_tabs=0",
           "nospace_before_paren=0",
           "indent_comments=1",
           "indent_case=1",
           "statement_comment_col=0",
           "disable_bestyle=0",
           "decl_comment_col=0",
           "bestyle_on_functions=0",
           "use_relative_indent=1",
           "nospace_before_brace=0",
           "indent_fl=1",
           "statement_comment_state=2",
           "indent_pp=1",
           "be_style=1",
           "parens_on_return=0",
           "eat_blank_lines=0",
           "brace_indent=0",
           "eat_pp_space=1",
           "align_on_parens=1",
           "continuation_indent=0",
           "cuddle_else=0",
           "nopad_condition=1",
           "pad_condition=0",
           "indent_col1_comments=0"
        }
    }
    ,
    {
        "Opt2Ind3",
        {
           "orig_tabsize=3",
           "syntax_indent=3",
           "tabsize=3",
           "align_on_equal=1",
           "pad_condition_state=1",
           "indent_with_tabs=0",
           "nospace_before_paren=0",
           "indent_comments=1",
           "indent_case=1",
           "statement_comment_col=0",
           "disable_bestyle=0",
           "decl_comment_col=0",
           "bestyle_on_functions=0",
           "use_relative_indent=1",
           "nospace_before_brace=0",
           "indent_fl=1",
           "statement_comment_state=2",
           "indent_pp=1",
           "be_style=1",
           "parens_on_return=0",
           "eat_blank_lines=0",
           "brace_indent=0",
           "eat_pp_space=1",
           "align_on_parens=1",
           "continuation_indent=0",
           "cuddle_else=0",
           "nopad_condition=1",
           "pad_condition=0",
           "indent_col1_comments=0"
        }
    }
    ,
    {
        "Opt2Ind8",
        {
           "orig_tabsize=8",
           "syntax_indent=8",
           "tabsize=8",
           "align_on_equal=1",
           "pad_condition_state=1",
           "indent_with_tabs=0",
           "nospace_before_paren=0",
           "indent_comments=1",
           "indent_case=1",
           "statement_comment_col=0",
           "disable_bestyle=0",
           "decl_comment_col=0",
           "bestyle_on_functions=0",
           "use_relative_indent=1",
           "nospace_before_brace=0",
           "indent_fl=1",
           "statement_comment_state=2",
           "indent_pp=1",
           "be_style=1",
           "parens_on_return=0",
           "eat_blank_lines=0",
           "brace_indent=0",
           "eat_pp_space=1",
           "align_on_parens=1",
           "continuation_indent=0",
           "cuddle_else=0",
           "nopad_condition=1",
           "pad_condition=0",
           "indent_col1_comments=0"
        }
    }
    ,
    {
        "Opt3Ind4",
        {
           "orig_tabsize=4",
           "syntax_indent=4",
           "tabsize=4",
           "align_on_equal=1",
           "pad_condition_state=1",
           "indent_with_tabs=0",
           "nospace_before_paren=0",
           "indent_comments=1",
           "indent_case=1",
           "statement_comment_col=0",
           "disable_bestyle=0",
           "decl_comment_col=0",
           "bestyle_on_functions=0",
           "use_relative_indent=1",
           "nospace_before_brace=0",
           "indent_fl=1",
           "statement_comment_state=2",
           "indent_pp=1",
           "be_style=2",
           "parens_on_return=0",
           "eat_blank_lines=0",
           "brace_indent=0",
           "eat_pp_space=1",
           "align_on_parens=1",
           "continuation_indent=0",
           "cuddle_else=0",
           "nopad_condition=1",
           "pad_condition=0",
           "indent_col1_comments=0"
        }
    }
    ,
    {
        "Opt3Ind3",
        {
            "orig_tabsize=3",
            "syntax_indent=3",
            "tabsize=3",
            "align_on_equal=1",
            "pad_condition_state=1",
            "indent_with_tabs=0",
            "nospace_before_paren=0",
            "indent_comments=1",
            "indent_case=1",
            "statement_comment_col=0",
            "disable_bestyle=0",
            "decl_comment_col=0",
            "bestyle_on_functions=0",
            "use_relative_indent=1",
            "nospace_before_brace=0",
            "indent_fl=1",
            "statement_comment_state=2",
            "indent_pp=1",
            "be_style=2",
            "parens_on_return=0",
            "eat_blank_lines=0",
            "brace_indent=0",
            "eat_pp_space=1",
            "align_on_parens=1",
            "continuation_indent=0",
            "cuddle_else=0",
            "nopad_condition=1",
            "pad_condition=0",
            "indent_col1_comments=0"
        }
    }
};


static void k_styles_create()
{
    /*
     * Find user format ini file.
     */
    _str userini = maybe_quote_filename(_config_path():+'uformat.ini');
    if (file_match('-p 'userini, 1) == '')
    {
        _str ini = maybe_quote_filename(slick_path_search('uformat.ini'));
        if (ini != '') userini = ini;
    }


    /*
     * Remove any old schemes.
     */
    int i,j,tv;
    for (i = 0; i < StyleSchemes._length(); i++)
        for (j = 0; j < StyleLanguages._length(); j++)
        {
            _str sectionname = StyleLanguages[j]:+'-scheme-':+StyleSchemes[i].name;
            if (!_ini_get_section(userini, sectionname, tv))
            {
                _ini_delete_section(userini, sectionname);
                _delete_temp_view(tv);
                //message("delete old scheme");
            }
        }

    /*
     * Create the new schemes.
     */
    for (i = 0; i < StyleSchemes._length(); i++)
    {
        for (j = 0; j < StyleLanguages._length(); j++)
        {
            _str sectionname = StyleLanguages[j]:+'-scheme-':+StyleSchemes[i].name;
            int temp_view_id, k;
            _str orig_view_id = _create_temp_view(temp_view_id);
            activate_view(temp_view_id);
            for (k = 0; k < StyleSchemes[i].settings._length(); k++)
                insert_line(StyleSchemes[i].settings[k]);

            /* Insert the scheme section. */
            _ini_replace_section(userini, sectionname, temp_view_id);
            //message(userini)
            //bogus id - activate_view(orig_view_id);
        }
    }

    //last_scheme = last scheme name!!!
}


/*
 * Sets the last used beutify scheme.
 */
static k_styles_set(_str scheme)
{

    /*
     * Find user format ini file.
     */
    _str userini = maybe_quote_filename(_config_path():+'uformat.ini');
    if (file_match('-p 'userini, 1) == '')
    {
        _str ini = maybe_quote_filename(slick_path_search('uformat.ini'));
        if (ini != '') userini = ini;
    }

    /*
     * Set the scheme for each language.
     */
    int j;
    for (j = 0; j < StyleLanguages._length(); j++)
    {
        _ini_set_value(userini,
                       StyleLanguages[j]:+'-scheme-Default',
                       'last_scheme',
                       scheme);
    }
}


static _str defoptions[] =
{
    "def-options-sas",
    "def-options-js",
    "def-options-bat",
    "def-options-c",
    "def-options-pas",
    "def-options-e",
    "def-options-java",
    "def-options-bourneshell",
    "def-options-csh",
    "def-options-vlx",
    "def-options-plsql",
    "def-options-sqlserver",
    "def-options-cmd"
};

static _str defsetups[] =
{
    "def-setup-sas",
    "def-setup-js",
    "def-setup-bat",
    "def-setup-fundamental",
    "def-setup-process",
    "def-setup-c",
    "def-setup-pas",
    "def-setup-e",
    "def-setup-asm",
    "def-setup-java",
    "def-setup-html",
    "def-setup-bourneshell",
    "def-setup-csh",
    "def-setup-vlx",
    "def-setup-fileman",
    "def-setup-plsql",
    "def-setup-sqlserver",
    "def-setup-s",
    "def-setup-cmd"
};

static _str defsetupstab8[] =
{
    "def-setup-c"
};


static void k_styles_setindent(int indent, int iBraceStyle, boolean iWithTabs = false)
{
    if (iBraceStyle < 1 || iBraceStyle > 3)
    {
        message('k_styles_setindent: iBraceStyle is bad (=' :+ iBraceStyle :+ ')');
        iBraceStyle = 2;
    }

    /*
     * def-options for extentions known to have that info.
     */
    int i;
    for (i = 0; i < defoptions._length(); i++)
    {
        int idx = find_index(defoptions[i], MISC_TYPE);
        if (!idx)
            continue;

        parse name_info(idx) with syntax_indent o2 o3 o4 flags indent_fl o7 indent_case rest;

        /* Begin/end style */
        flags = flags & ~(1|2);
        flags = flags | (iBraceStyle - 1); /* Set style (0-based) */
        flags = flags & ~(16); /* no scape before parent.*/
        indent_fl = 1;         /* Indent first level */
        indent_case = 1;       /* Indent case from switch */

        sNewOptions = indent' 'o2' 'o3' 'o4' 'flags' 'indent_fl' 'o7' 'indent_case' 'rest;
        set_name_info(idx, sNewOptions);
        _config_modify |= CFGMODIFY_DEFDATA;
    }

    /*
     * def-setup for known extentions.
     */
    for (i = 0; i < defsetups._length(); i++)
    {
        idx = find_index(defsetups[i], MISC_TYPE);
        if (!idx)
           continue;
        sExt = substr(defsetups[i], length('def-setup-') + 1);
        sSetup = name_info(idx);

        /*
        parse sSetup with 'MN=' mode_name ','\
          'TABS=' tabs ',' 'MA=' margins ',' 'KEYTAB=' keytab_name ','\
          'WW='word_wrap_style ',' 'IWT='indent_with_tabs ','\
          'ST='show_tabs ',' 'IN='indent_style ','\
          'WC='word_chars',' 'LN='lexer_name',' 'CF='color_flags','\
          'LNL='line_numbers_len','rest;

        indent_with_tabs = 0; /* Indent with tabs */

        /* Make sure all the values are legal */
        _ext_init_values(ext, lexer_name, color_flags);
        if (!isinteger(line_numbers_len))   line_numbers_len = 0;
        if (word_chars == '')               word_chars       = 'A-Za-z0-9_$';
        if (word_wrap_style == '')          word_wrap_style  = 3;
        if (show_tabs == '')                show_tabs        = 0;
        if (indent_style == '')             indent_style     = INDENT_SMART;

        /* Set new indent */
        tabs = '+'indent;
        */

        sNewSetup = sSetup;

        /* Set new indent */
        if (pos('TABS=', sNewSetup) > 0)
        {
            /*
             * If either in defoptions or defsetupstab8 use default tab of 8
             * For those supporting separate syntax indent using the normal tabsize
             * helps us a lot when reading it...
             */
            fTab8 = false;
            for (j = 0; !fTab8 && j < defsetupstab8._length(); j++)
                if (substr(defsetupstab8[j], lastpos('-', defsetupstab8[j]) + 1) == sExt)
                    fTab8 = true;
            for (j = 0; !fTab8 && j < defoptions._length(); j++)
                if (substr(defoptions[j], lastpos('-', defoptions[j]) + 1) == sExt)
                    fTab8 = true;

            parse sNewSetup with sPre 'TABS=' sValue ',' sPost;
            if (fTab8)
                sNewSetup = sPre 'TABS=+8,' sPost
            else
                sNewSetup = sPre 'TABS=+' indent ',' sPost
        }

        /* Set indent with tabs flag. */
        if (pos('IWT=', sNewSetup) > 0)
        {
            parse sNewSetup with sPre 'IWT=' sValue ',' sPost;
            if (iWithTabs)
                sNewSetup = sPre 'IWT=1,' sPost
            else
                sNewSetup = sPre 'IWT=0,' sPost
        }

        /* Do the real changes */
        set_name_info(idx, sNewSetup);
        _config_modify |= CFGMODIFY_DEFDATA;
        _update_buffers(sExt);
    }
}


/**
 * Takes necessary steps to convert a string to integer.
 */
static int k_style_emacs_var_integer(_str sVal)
{
    int i = (int)sVal;
    //say 'k_style_emacs_var_integer('sVal') -> 'i;
    return (int)sVal;
}


/**
 * Sets a Emacs style variable.
 */
static int k_style_emacs_var(_str sVar, _str sVal)
{
    /* check input. */
    if (sVar == '' || sVal == '')
        return -1;
    //say 'k_style_emacs_var: 'sVar'='sVal;

    /*
     * Unpack the mode style parameters.
     */
    _str sStyle = name_info(_edit_window().p_index);
    _str sStyleName = p_mode_name;
    typeless iIndentAmount, fExpansion, iMinAbbrivation, fIndentAfterOpenParen, iBeginEndStyle, fIndent1stLevel, iMainStyle, iSwitchStyle,
             sRest, sRes0, sRes1;
    if (sStyleName == 'Slick-C')
    {
         parse sStyle with iMinAbbrivation sRes0 iBeginEndStyle fIndent1stLevel sRes1 iSwitchStyle sRest;
         iIndentAmount = p_SyntaxIndent;
    }
    else /* C */
         parse sStyle with iIndentAmount fExpansion iMinAbbrivation fIndentAfterOpenParen iBeginEndStyle fIndent1stLevel iMainStyle iSwitchStyle sRest;


    /*
     * Process the variable.
     */
    switch (sVar)
    {
        case 'mode':
        case 'Mode':
        {
            switch (sVal)
            {
                case 'c':
                case 'C':
                case 'c++':
                case 'C++':
                case 'cpp':
                case 'CPP':
                case 'cxx':
                case 'CXX':
                    p_extension = 'c';
                    p_mode_name = 'C';
                    break;

                case 'e':
                case 'slick-c':
                case 'Slick-c':
                case 'Slick-C':
                    p_extension = 'e';
                    p_mode_name = 'Slick-C';
                    break;

                default:
                    message('emacs mode "'sVal'" is not known to us');
                    return -3;
            }
            break;
        }
/* relevant emacs code:
(defconst c-style-alist
  '(("gnu"
     (c-basic-offset . 2)
     (c-comment-only-line-offset . (0 . 0))
     (c-offsets-alist . ((statement-block-intro . +)
			 (knr-argdecl-intro . 5)
			 (substatement-open . +)
			 (label . 0)
			 (statement-case-open . +)
			 (statement-cont . +)
			 (arglist-intro . c-lineup-arglist-intro-after-paren)
			 (arglist-close . c-lineup-arglist)
			 (inline-open . 0)
			 (brace-list-open . +)
			 ))
     (c-special-indent-hook . c-gnu-impose-minimum)
     (c-block-comment-prefix . "")
     )
    ("k&r"
     (c-basic-offset . 5)
     (c-comment-only-line-offset . 0)
     (c-offsets-alist . ((statement-block-intro . +)
			 (knr-argdecl-intro . 0)
			 (substatement-open . 0)
			 (label . 0)
			 (statement-cont . +)
			 ))
     )
    ("bsd"
     (c-basic-offset . 8)
     (c-comment-only-line-offset . 0)
     (c-offsets-alist . ((statement-block-intro . +)
			 (knr-argdecl-intro . +)
			 (substatement-open . 0)
			 (label . 0)
			 (statement-cont . +)
			 (inline-open . 0)
			 (inexpr-class . 0)
			 ))
     )
    ("stroustrup"
     (c-basic-offset . 4)
     (c-comment-only-line-offset . 0)
     (c-offsets-alist . ((statement-block-intro . +)
			 (substatement-open . 0)
			 (label . 0)
			 (statement-cont . +)
			 ))
     )
    ("whitesmith"
     (c-basic-offset . 4)
     (c-comment-only-line-offset . 0)
     (c-offsets-alist . ((knr-argdecl-intro . +)
			 (label . 0)
			 (statement-cont . +)
			 (substatement-open . +)
			 (block-open . +)
			 (statement-block-intro . c-lineup-whitesmith-in-block)
			 (block-close . c-lineup-whitesmith-in-block)
			 (inline-open . +)
			 (defun-open . +)
			 (defun-block-intro . c-lineup-whitesmith-in-block)
			 (defun-close . c-lineup-whitesmith-in-block)
			 (brace-list-open . +)
			 (brace-list-intro . c-lineup-whitesmith-in-block)
			 (brace-entry-open . c-indent-multi-line-block)
			 (brace-list-close . c-lineup-whitesmith-in-block)
			 (class-open . +)
			 (inclass . c-lineup-whitesmith-in-block)
			 (class-close . +)
			 (inexpr-class . 0)
			 (extern-lang-open . +)
			 (inextern-lang . c-lineup-whitesmith-in-block)
			 (extern-lang-close . +)
			 (namespace-open . +)
			 (innamespace . c-lineup-whitesmith-in-block)
			 (namespace-close . +)
			 ))
     )
    ("ellemtel"
     (c-basic-offset . 3)
     (c-comment-only-line-offset . 0)
     (c-hanging-braces-alist     . ((substatement-open before after)))
     (c-offsets-alist . ((topmost-intro        . 0)
                         (topmost-intro-cont   . 0)
                         (substatement         . +)
			 (substatement-open    . 0)
                         (case-label           . +)
                         (access-label         . -)
                         (inclass              . ++)
                         (inline-open          . 0)
                         ))
     )
    ("linux"
     (c-basic-offset  . 8)
     (c-comment-only-line-offset . 0)
     (c-hanging-braces-alist . ((brace-list-open)
				(brace-entry-open)
				(substatement-open after)
				(block-close . c-snug-do-while)))
     (c-cleanup-list . (brace-else-brace))
     (c-offsets-alist . ((statement-block-intro . +)
			 (knr-argdecl-intro     . 0)
			 (substatement-open     . 0)
			 (label                 . 0)
			 (statement-cont        . +)
			 ))
     )
    ("python"
     (indent-tabs-mode . t)
     (fill-column      . 78)
     (c-basic-offset   . 8)
     (c-offsets-alist  . ((substatement-open . 0)
			  (inextern-lang . 0)
			  (arglist-intro . +)
			  (knr-argdecl-intro . +)
			  ))
     (c-hanging-braces-alist . ((brace-list-open)
				(brace-list-intro)
				(brace-list-close)
				(brace-entry-open)
				(substatement-open after)
				(block-close . c-snug-do-while)
				))
     (c-block-comment-prefix . "")
     )
    ("java"
     (c-basic-offset . 4)
     (c-comment-only-line-offset . (0 . 0))
     ;; the following preserves Javadoc starter lines
     (c-offsets-alist . ((inline-open . 0)
			 (topmost-intro-cont    . +)
			 (statement-block-intro . +)
 			 (knr-argdecl-intro     . 5)
 			 (substatement-open     . +)
 			 (label                 . +)
 			 (statement-case-open   . +)
 			 (statement-cont        . +)
 			 (arglist-intro  . c-lineup-arglist-intro-after-paren)
 			 (arglist-close  . c-lineup-arglist)
 			 (access-label   . 0)
			 (inher-cont     . c-lineup-java-inher)
			 (func-decl-cont . c-lineup-java-throws)
			 ))
     )
    )
*/

        case 'c-file-style':
        case 'c-indentation-style':
            switch (sVal)
            {
                case 'bsd':
                case '"bsd"':
                case 'BSD':
                    iBeginEndStyle = 1 | (iBeginEndStyle & ~3);
                    p_indent_with_tabs = true;
                    iIndentAmount = 8;
                    p_SyntaxIndent = 8;
                    p_tabs = "+8";
                    //say 'bsd';
                    break;

                case 'k&r':
                case '"k&r"':
                case 'K&R':
                    iBeginEndStyle = 0 | (iBeginEndStyle & ~3);
                    p_indent_with_tabs = false;
                    iIndentAmount = 4;
                    p_SyntaxIndent = 4;
                    p_tabs = "+4";
                    //say 'k&r';
                    break;

                case 'linux-c':
                case '"linux-c"':
                    iBeginEndStyle = 0 | (iBeginEndStyle & ~3);
                    p_indent_with_tabs = true;
                    iIndentAmount = 4;
                    p_SyntaxIndent = 4;
                    p_tabs = "+4";
                    //say 'linux-c';
                    break;

                case 'yet-to-be-found':
                    iBeginEndStyle = 2 | (iBeginEndStyle & ~3);
                    p_indent_with_tabs = false;
                    iIndentAmount = 4;
                    p_SyntaxIndent = 4;
                    p_tabs = "+4";
                    //say 'todo';
                    break;

                default:
                    message('emacs "'sVar'" value "'sVal'" is not known to us.');
                    return -3;
            }
            break;

        case 'c-label-offset':
        {
            int i = k_style_emacs_var_integer(sVal);
            if (i >= -16 && i <= 16)
            {
                if (i == -p_SyntaxIndent)
                    iSwitchStyle = 0;
                else
                    iSwitchStyle = 1;
            }
            break;
        }


        case 'indent-tabs-mode':
            p_indent_with_tabs = sVal == 't';
            break;

        case 'c-indent-level':
        case 'c-basic-offset':
        {
            int i = k_style_emacs_var_integer(sVal);
            if (i > 0 && i <= 16)
            {
                iIndentAmount = i;
                p_SyntaxIndent = i;
            }
            else
            {
                message('emacs "'sVar'" value "'sVal'" is out of range.');
                return -4;
            }
            break;
        }

        case 'tab-width':
        {
            int i = k_style_emacs_var_integer(sVal);
            if (i > 0 && i <= 16)
                p_tabs = '+'i;
            else
            {
                message('emacs "'sVar'" value "'sVal'" is out of range.');
                return -4;
            }
            break;
        }

        case 'nuke-trailing-whitespace-p':
        {
#if 0
            _str sName = 'def-koptions-'p_buf_id;
            int idx = insert_name(sName, MISC_TYPE, "kstyledoc");
            if (!idx)
                idx = find_index(sName, MISC_TYPE);
            if (idx)
            {
                if (sVal == 't')
                    set_name_info(idx, "saveoptions: +S");
                else
                    set_name_info(idx, "saveoptions: -S");
                say 'sVal=' sVal;
            }
#endif
            break;
        }

        default:
            message('emacs variable "'sVar'" (value "'sVal'") is unknown to us.');
            return -5;
    }

    /*
     * Update the style?
     */
    _str sNewStyle = "";
    if (sStyleName == 'Slick-C')
        sNewStyle = iMinAbbrivation' 'sRes0' 'iBeginEndStyle' 'fIndent1stLevel' 'sRes1' 'iSwitchStyle' 'sRest;
    else
        sNewStyle = iIndentAmount' 'fExpansion' 'iMinAbbrivation' 'fIndentAfterOpenParen' 'iBeginEndStyle' 'fIndent1stLevel' 'iMainStyle' 'iSwitchStyle' 'sRest;
    if (   sNewStyle != ""
        && sNewStyle != sStyle
        && sStyleName == p_mode_name)
    {
        _str sName = name_name(_edit_window().p_index)
        //say '   sStyle='sStyle' p_mode_name='p_mode_name;
        //say 'sNewStyle='sNewStyle' sName='sName;
        if (pos('kstyledoc-', sName) <= 0)
        {
            sName = 'def-kstyledoc-'p_buf_id;
            int idx = insert_name(sName, MISC_TYPE, "kstyledoc");
            if (!idx)
                idx = find_index(sName, MISC_TYPE);
            if (idx)
            {
                if (!set_name_info(idx, sNewStyle))
                    _edit_window().p_index = idx;
            }
            //say sName'='idx;
        }
        else
            set_name_info(_edit_window().p_index, sNewStyle);
    }

    return 0;
}


/**
 * Parses a string with emacs variables.
 *
 * The variables are separated by new line. Junk at
 * the start and end of the line is ignored.
 */
static int k_style_emac_vars(_str sVars)
{
    /* process them line by line */
    int iLine = 0;
    while (sVars != '' && iLine++ < 20)
    {
        int iNext, iEnd;
        iEnd = iNext = pos("\n", sVars);
        if (iEnd <= 0)
            iEnd = iNext = length(sVars);
        else
            iEnd--;
        iNext++;

        sLine = strip(substr(sVars, 1, iEnd), 'B', " \t\n\r");
        sVars = strip(substr(sVars, iNext), 'L', " \t\n\r");
        //say 'iLine='iLine' sVars='sVars'<eol>';
        //say 'iLine='iLine' sLine='sLine'<eol>';
        if (sLine != '')
        {
            rc = pos('[^a-zA-Z0-9-_]*([a-zA-Z0-9-_]+)[ \t]*:[ \t]*([^ \t]*)', sLine, 1, 'U');
            //say '0={'pos('S0')','pos('0')',"'substr(sLine,pos('S0'),pos('0'))'"'
            //say '1={'pos('S1')','pos('1')',"'substr(sLine,pos('S1'),pos('1'))'"'
            //say '2={'pos('S2')','pos('2')',"'substr(sLine,pos('S2'),pos('2'))'"'
            //say '3={'pos('S3')','pos('3')',"'substr(sLine,pos('S3'),pos('3'))'"'
            //say '4={'pos('S4')','pos('4')',"'substr(sLine,pos('S4'),pos('4'))'"'
            if (rc > 0)
                k_style_emacs_var(substr(sLine,pos('S1'),pos('1')),
                                  substr(sLine,pos('S2'),pos('2')));
        }
    }
    return 0;
}

/**
 * Searches for Emacs style specification for the current document.
 */
void k_style_load()
{
    /* save the position before we start looking around the file. */
    typeless saved_pos;
    _save_pos2(saved_pos);

    int rc;

    /* Check first line. */
    top_of_buffer();
    _str sLine;
    get_line(sLine);
    strip(sLine);
    if (pos('-*-[ \t]+(.*:.*)[ \t]+-*-', sLine, 1, 'U'))
    {
        _str sVars;
        sVars = substr(sLine, pos('S1'), pos('1'));
        sVars = translate(sVars, "\n", ";");
        k_style_emac_vars(sVars);
    }

    /* Look for the "Local Variables:" stuff from the end of the file. */
    bottom_of_buffer();
    rc = search('Local Variables:[ \t]*\n\om(.*)\ol\n.*End:.*\n', '-EU');
    if (!rc)
    {
        /* copy the variables out to a buffer. */
        _str sVars;
        sVars = get_text(match_length("1"), match_length("S1"));
        k_style_emac_vars(sVars);
    }

    _restore_pos2(saved_pos);
}


/**
 * Callback function for the event of a new buffer.
 *
 * This is used to make sure there are no left over per buffer options
 * hanging around.
 */
void _buffer_add_kdev(int buf_id)
{
    _str sName = 'def-koptions-'buf_id;
    int idx = find_index(sName, MISC_TYPE);
    if (idx)
        delete_name(idx);
    //message("_buffer_add_kdev: " idx " name=" sName);

    sName = 'def-kstyledoc-'buf_id;
    idx = find_index(sName, MISC_TYPE);
    if (idx)
        delete_name(idx);

    //k_style_load();
}


/**
 * Callback function for the event of quitting a buffer.
 *
 * This is used to make sure there are no left over per buffer options
 * hanging around.
 */
void _cbquit2_kdev(int buf_id)
{
    _str sName = 'def-koptions-'buf_id;
    int idx = find_index(sName, MISC_TYPE);
    if (idx)
        delete_name(idx);
    //message("_cbquit2_kdev: " idx " " sName);

    sName = 'def-kstyledoc-'buf_id;
    idx = find_index(sName, MISC_TYPE);
    if (idx)
        delete_name(idx);
}


/**
 * Called to get save options for the current buffer.
 *
 * This requires a modified loadsave.e!
 */
_str _buffer_save_kdev(int buf_id)
{
    _str sRet = ""
    _str sName = 'def-koptions-'buf_id;
    int idx = find_index(sName, MISC_TYPE);
    if (idx)
    {
        _str sOptions = strip(name_info(idx));
        if (sOptions != "")
            parse sOptions with . "saveoptions:" sRet .
        message("_buffer_save_kdev: " idx " " sName " " sOptions);
    }
    return sRet;
}



/*******************************************************************************
*   Menu and Menu commands                                                     *
*******************************************************************************/
static int  iTimer = 0;
static int  mhkDev = 0;
static int  mhCode = 0;
static int  mhDoc = 0;
static int  mhLic = 0;
static int  mhPre = 0;

/*
 * Creates the kDev menu.
 */
static k_menu_create()
{
    if (arg(1) == 'timer')
        _kill_timer(iTimer);
    menu_handle = _mdi.p_menu_handle;
    menu_index  = find_index(_cur_mdi_menu,oi2type(OI_MENU));

    /*
     * Remove any old menu.
     */
    mhDelete = iPos = 0;
    index = _menu_find(menu_handle, "kDev", mhDelete, iPos, 'C');
    //message("index="index " mhDelete="mhDelete " iPos="iPos);
    if (index == 0)
        _menu_delete(mhDelete, iPos);


    /*
     * Insert the "kDev" menu.
     */
    mhkDev = _menu_insert(menu_handle, 9, MF_SUBMENU, "&kDev", "", "kDev");
    mhCode=_menu_insert(mhkDev,  -1, MF_ENABLED | MF_SUBMENU,   "Coding &Style",  "", "coding");
    rc   = _menu_insert(mhCode,  -1, MF_ENABLED | MF_UNCHECKED, "Braces 2, Syntax Indent 4 (knut)",    "k_menu_style Opt2Ind4",    "Opt2Ind4");
    rc   = _menu_insert(mhCode,  -1, MF_ENABLED | MF_UNCHECKED, "Braces 2, Syntax Indent 3",           "k_menu_style Opt2Ind3",    "Opt2Ind3");
    rc   = _menu_insert(mhCode,  -1, MF_ENABLED | MF_UNCHECKED, "Braces 2, Syntax Indent 8",           "k_menu_style Opt2Ind8",    "Opt2Ind8");
    rc   = _menu_insert(mhCode,  -1, MF_ENABLED | MF_UNCHECKED, "Braces 3, Syntax Indent 4 (giws)",    "k_menu_style Opt3Ind4",    "Opt3Ind4");
    rc   = _menu_insert(mhCode,  -1, MF_ENABLED | MF_UNCHECKED, "Braces 3, Syntax Indent 3 (giws)",    "k_menu_style Opt3Ind3",    "Opt3Ind3");

    mhDoc= _menu_insert(mhkDev,  -1, MF_ENABLED | MF_SUBMENU,   "&Documentation",       "",                             "doc");
    mhDSJ= _menu_insert(mhDoc,   -1, MF_ENABLED | MF_UNCHECKED, "&Javadoc Style",       "k_menu_doc_style javadoc",     "javadoc");
    mhDSL= _menu_insert(mhDoc,   -1, MF_GRAYED  | MF_UNCHECKED, "&Linux Kernel Style",  "k_menu_doc_style linux",       "linux");

    mhLic= _menu_insert(mhkDev,  -1, MF_ENABLED | MF_SUBMENU,   "&License",             "",                             "License");
    rc   = _menu_insert(mhLic,   -1, MF_ENABLED | MF_UNCHECKED, "&Odin32",              "k_menu_license Odin32",        "Odin32");
    rc   = _menu_insert(mhLic,   -1, MF_ENABLED | MF_UNCHECKED, "&GPL",                 "k_menu_license GPL",           "GPL");
    rc   = _menu_insert(mhLic,   -1, MF_ENABLED | MF_UNCHECKED, "&LGPL",                "k_menu_license LGPL",          "LGPL");
    rc   = _menu_insert(mhLic,   -1, MF_ENABLED | MF_UNCHECKED, "&GPLv3",               "k_menu_license GPLv3",         "GPLv3");
    rc   = _menu_insert(mhLic,   -1, MF_ENABLED | MF_UNCHECKED, "&LGPLv3",              "k_menu_license LGPLv3",        "LGPLv3");
    rc   = _menu_insert(mhLic,   -1, MF_ENABLED | MF_UNCHECKED, "&VirtualBox",          "k_menu_license VirtualBox",    "VirtualBox");
    rc   = _menu_insert(mhLic,   -1, MF_ENABLED | MF_UNCHECKED, "&VirtualBox GPL And CDDL","k_menu_license VirtualBoxGPLAndCDDL", "VirtualBoxGPLAndCDDL");
    rc   = _menu_insert(mhLic,   -1, MF_ENABLED | MF_UNCHECKED, "&Confidential",        "k_menu_license Confidential",  "Confidential");
    rc   = _menu_insert(mhLic,   -1, MF_ENABLED | MF_UNCHECKED, "&Confidential No Author", "k_menu_license ConfidentialNoAuthor",  "ConfidentialNoAuthor");

    rc   = _menu_insert(mhkDev,  -1, MF_ENABLED, "-", "", "dash vars");
    rc   = _menu_insert(mhkDev,  -1, MF_ENABLED, skChange  == '' ? '&Change...'  : '&Change (' skChange ')...',   "k_menu_change", "");
    rc   = _menu_insert(mhkDev,  -1, MF_ENABLED, skProgram == '' ? '&Program...' : '&Program (' skProgram ')...', "k_menu_program", "");
    rc   = _menu_insert(mhkDev,  -1, MF_ENABLED, skCompany == '' ? 'Co&mpany...' : 'Co&mpany (' skCompany ')...', "k_menu_company", "");
    rc   = _menu_insert(mhkDev,  -1, MF_ENABLED, '&User Name (' skUserName ')...',          "k_menu_user_name",     "username");
    rc   = _menu_insert(mhkDev,  -1, MF_ENABLED, 'User &e-mail (' skUserEmail ')...',       "k_menu_user_email",    "useremail");
    rc   = _menu_insert(mhkDev,  -1, MF_ENABLED, 'User &Initials (' skUserInitials ')...',  "k_menu_user_initials", "userinitials");
    rc   = _menu_insert(mhkDev,  -1, MF_ENABLED, "-", "", "dash preset");
    mhPre= _menu_insert(mhkDev,  -1, MF_SUBMENU, "P&resets", "", "");
    rc   = _menu_insert(mhPre,   -1, MF_ENABLED, "The Bird",    "k_menu_preset javadoc, GPL, Opt2Ind4",                         "bird");
    rc   = _menu_insert(mhPre,   -1, MF_ENABLED, "kLIBC",       "k_menu_preset javadoc, GPL, Opt2Ind4,, kLIBC",                 "kLIBC");
    rc   = _menu_insert(mhPre,   -1, MF_ENABLED, "kBuild",      "k_menu_preset javadoc, GPLv3, Opt2Ind4,, kBuild",              "kBuild");
    rc   = _menu_insert(mhPre,   -1, MF_ENABLED, "kStuff",      "k_menu_preset javadoc, GPL, Opt2Ind4,, kStuff",                "kStuff");
    rc   = _menu_insert(mhPre,   -1, MF_ENABLED, "sun",         "k_menu_preset javadoc, ConfidentialNoAuthor, Opt2Ind4, sun",   "sun");
    rc   = _menu_insert(mhPre,   -1, MF_ENABLED, "VirtualBox",  "k_menu_preset javadoc, VirtualBox, Opt2Ind4, sun",             "VirtualBox");

    k_menu_doc_style();
    k_menu_license();
    k_menu_style();
}


/**
 * Change change Id.
 */
_command k_menu_change()
{
    sRc = show("-modal k_form_simple_input", "Change ID", skChange);
    if (sRc != "\r")
    {
        skChange = sRc;
        k_menu_create();
    }
}


/**
 * Change program name.
 */
_command k_menu_program()
{
    sRc = show("-modal k_form_simple_input", "Program", skProgram);
    if (sRc != "\r")
    {
        skProgram = sRc;
        k_menu_create();
    }
}


/**
 * Change company.
 */
_command k_menu_company()
{
    if (skCompany == '')
        sRc = show("-modal k_form_simple_input", "Company", 'innotek GmbH');
    else
        sRc = show("-modal k_form_simple_input", "Company", skCompany);
    if (sRc != "\r")
    {
        skCompany = sRc;
        k_menu_create();
    }
}


/**
 * Change user name.
 */
_command k_menu_user_name()
{
    sRc = show("-modal k_form_simple_input", "User Name", skUserName);
    if (sRc != "\r" && sRc != '')
    {
        skUserName = sRc;
        k_menu_create();
    }
}


/**
 * Change user email.
 */
_command k_menu_user_email()
{
    sRc = show("-modal k_form_simple_input", "User e-mail", skUserEmail);
    if (sRc != "\r" && sRc != '')
    {
        skUserEmail = sRc;
        k_menu_create();
    }
}


/**
 * Change user initials.
 */
_command k_menu_user_initials()
{
    sRc = show("-modal k_form_simple_input", "User e-mail", skUserInitials);
    if (sRc != "\r" && sRc != '')
    {
        skUserInitials = sRc;
        k_menu_create();
    }
}



/**
 * Checks the correct menu item.
 */
_command void k_menu_doc_style(_str sNewDocStyle = '')
{
    //say 'sNewDocStyle='sNewDocStyle;
    if (sNewDocStyle != '')
        skDocStyle = sNewDocStyle
    _menu_set_state(mhDoc, "javadoc",   MF_UNCHECKED);
    _menu_set_state(mhDoc, "linux",     MF_UNCHECKED | MF_GRAYED);

    _menu_set_state(mhDoc, skDocStyle,  MF_CHECKED);
}


/**
 * Checks the correct menu item.
 */
_command void k_menu_license(_str sNewLicense = '')
{
    //say 'sNewLicense='sNewLicense;
    if (sNewLicense != '')
        skLicense = sNewLicense
    _menu_set_state(mhLic, "Odin32",        MF_UNCHECKED);
    _menu_set_state(mhLic, "GPL",           MF_UNCHECKED);
    _menu_set_state(mhLic, "LGPL",          MF_UNCHECKED);
    _menu_set_state(mhLic, "GPLv3",         MF_UNCHECKED);
    _menu_set_state(mhLic, "LGPLv3",        MF_UNCHECKED);
    _menu_set_state(mhLic, "VirtualBox",    MF_UNCHECKED);
    _menu_set_state(mhLic, "VirtualBoxGPLAndCDDL", MF_UNCHECKED);
    _menu_set_state(mhLic, "Confidential",  MF_UNCHECKED);
    _menu_set_state(mhLic, "ConfidentialNoAuthor", MF_UNCHECKED);

    _menu_set_state(mhLic, skLicense,       MF_CHECKED);
}


/**
 * Check the correct style menu item.
 */
_command void k_menu_style(_str sNewStyle = '')
{
    //say 'sNewStyle='sNewStyle;
    _menu_set_state(mhCode, "Opt1Ind4", MF_UNCHECKED);
    _menu_set_state(mhCode, "Opt1Ind3", MF_UNCHECKED);
    _menu_set_state(mhCode, "Opt1Ind8", MF_UNCHECKED);
    _menu_set_state(mhCode, "Opt2Ind4", MF_UNCHECKED);
    _menu_set_state(mhCode, "Opt2Ind3", MF_UNCHECKED);
    _menu_set_state(mhCode, "Opt2Ind8", MF_UNCHECKED);
    _menu_set_state(mhCode, "Opt3Ind4", MF_UNCHECKED);
    _menu_set_state(mhCode, "Opt3Ind3", MF_UNCHECKED);
    _menu_set_state(mhCode, "Opt3Ind8", MF_UNCHECKED);

    if (sNewStyle != '')
    {
        int iIndent = (int)substr(sNewStyle, 8, 1);
        int iBraceStyle = (int)substr(sNewStyle, 4, 1);
        skCodeStyle = sNewStyle;
        k_styles_setindent(iIndent, iBraceStyle);
        k_styles_set(sNewStyle);
    }

    _menu_set_state(mhCode, skCodeStyle, MF_CHECKED);
}


/**
 * Load a 'preset'.
 */
_command void k_menu_preset(_str sArgs = '')
{
    parse sArgs with sNewDocStyle ',' sNewLicense ',' sNewStyle ',' sNewCompany ',' sNewProgram ',' sNewChange
    sNewDocStyle= strip(sNewDocStyle);
    sNewLicense = strip(sNewLicense);
    sNewStyle   = strip(sNewStyle);
    sNewCompany = strip(sNewCompany);
    if (sNewCompany == 'sun')
        sNewCompany = 'Sun Microsystems, Inc.'
    sNewProgram = strip(sNewProgram);
    sNewChange  = strip(sNewChange);

    //say 'k_menu_preset('sNewDocStyle',' sNewLicense',' sNewStyle',' sNewCompany',' sNewProgram')';
    k_menu_doc_style(sNewDocStyle);
    k_menu_license(sNewLicense);
    k_menu_style(sNewStyle);
    skCompany = sNewCompany;
    skProgram = sNewProgram;
    skChange = sNewChange;
    k_menu_create();
}



/* future ones..
_command k_menu_setcolor()
{
    createMyColorSchemeAndUseIt();
}


_command k_menu_setkeys()
{
    rc = load("d:/knut/VSlickMacros/BoxerDef.e");
}

_command k_menu_settings()
{
    mySettings();
}
*/


/*******************************************************************************
*   Dialogs                                                                    *
*******************************************************************************/
_form k_form_simple_input {
   p_backcolor=0x80000005
   p_border_style=BDS_DIALOG_BOX
   p_caption='Simple Input'
   p_clip_controls=FALSE
   p_forecolor=0x80000008
   p_height=1120
   p_width=5020
   p_x=6660
   p_y=6680
   _text_box entText {
      p_auto_size=TRUE
      p_backcolor=0x80000005
      p_border_style=BDS_FIXED_SINGLE
      p_completion=NONE_ARG
      p_font_bold=FALSE
      p_font_italic=FALSE
      p_font_name='MS Sans Serif'
      p_font_size=8
      p_font_underline=FALSE
      p_forecolor=0x80000008
      p_height=270
      p_tab_index=1
      p_tab_stop=TRUE
      p_text='text'
      p_width=3180
      p_x=1680
      p_y=240
      p_eventtab2=_ul2_textbox
   }
   _label lblLabel {
      p_alignment=AL_VCENTERRIGHT
      p_auto_size=FALSE
      p_backcolor=0x80000005
      p_border_style=BDS_NONE
      p_caption='Label'
      p_font_bold=FALSE
      p_font_italic=FALSE
      p_font_name='MS Sans Serif'
      p_font_size=8
      p_font_underline=FALSE
      p_forecolor=0x80000008
      p_height=240
      p_tab_index=2
      p_width=1380
      p_word_wrap=FALSE
      p_x=180
      p_y=240
   }
   _command_button btnOK {
      p_cancel=FALSE
      p_caption='&OK'
      p_default=TRUE
      p_font_bold=FALSE
      p_font_italic=FALSE
      p_font_name='MS Sans Serif'
      p_font_size=8
      p_font_underline=FALSE
      p_height=360
      p_tab_index=3
      p_tab_stop=TRUE
      p_width=1020
      p_x=180
      p_y=660
   }
   _command_button btnCancel {
      p_cancel=TRUE
      p_caption='Cancel'
      p_default=FALSE
      p_font_bold=FALSE
      p_font_italic=FALSE
      p_font_name='MS Sans Serif'
      p_font_size=8
      p_font_underline=FALSE
      p_height=360
      p_tab_index=4
      p_tab_stop=TRUE
      p_width=840
      p_x=1380
      p_y=660
   }
}

defeventtab k_form_simple_input
btnOK.on_create(_str sLabel = '', _str sText = '')
{
    p_active_form.p_caption = sLabel;
    lblLabel.p_caption = sLabel;
    entText.p_text = sText;
}

btnOK.lbutton_up()
{
    sText = entText.p_text;
    p_active_form._delete_window(sText);
}
btnCancel.lbutton_up()
{
    sText = entText.p_text;
    p_active_form._delete_window("\r");
}

static _str aCLikeIncs[] = 
{ 
    "c", "ansic", "java", "rul", "vera", "ch", "as", "idl", "asm", "s", "imakefile", "rc", "lex", "yacc", "antlr"
};

static _str aMyLangIds[] =
{
    "ansic", 
    "antlr", 
    "as", 
    "asm", 
    "c", 
    "csh", 
    "css", 
    "ch", 
    "conf", 
    "d", 
    "docbook", 
    "dtd", 
    "html", 
    "idl", 
    "imakefile", 
    "ini", 
    "java", 
    "js", 
    "lex", 
    "mak", 
    "masm", 
    "phpscript", 
    "powershell", 
    "py",
    "rexx",
    "rc", 
    "rul", 
    "tcl", 
    "s", 
    "unixasm", 
    "vbs", 
    "xhtml", 
    "xml", 
    "xmldoc", 
    "xsd", 
    "yacc"
};

#if __VERSION__ >= 17.0
# require "se/lang/api/LanguageSettings.e"
using se.lang.api.LanguageSettings;
#endif

/**
 * Loads the standard bird settings.
 */
_command void kdev_load_settings()
{
    typeless nt1; 
    typeless nt2;
    typeless nt3;
    typeless nt4;
    typeless nt5;
    typeless nt6;
    typeless i7;
    _str sRest;
    _str sTmp;

    /*
     * Generl stuff.
     */
    _default_option('A', '0');          /* ALT menu */
    def_alt_menu = 0;
    _default_option('R', '130');        /* Vertical line in column 130. */
    def_mfsearch_init_flags = 2 | 4;    /* MFSEARCH_INIT_CURWORD | MFSEARCH_INIT_SELECTION */
    def_line_insert = 'B';              /* insert before */
    def_file_types='All Files (*),'     /** @todo make this prettier */
                   'C/C++ Files (*.c;*.cc;*.cpp;*.cp;*.cxx;*.c++;*.h;*.hh;*.hpp;*.hxx;*.inl;*.xpm),'
                   'Assembler (*.s;*.asm;*.mac;*.S),'
                   'Makefiles (*;*.mak;*.kmk)'
                   'C# Files (*.cs),'
                   'Ch Files (*.ch;*.chf;*.chs;*.cpp;*.h),'
                   'D Files (*.d),'
                   'Java Files (*.java),'
                   'HTML Files (*.htm;*.html;*.shtml;*.asp;*.jsp;*.php;*.php3;*.rhtml;*.css),'
                   'CFML Files (*.cfm;*.cfml;*.cfc),'
                   'XML Files (*.xml;*.dtd;*.xsd;*.xmldoc;*.xsl;*.xslt;*.ent;*.tld;*.xhtml;*.build;*.plist),'
                   'XML/SGML DTD Files (*.xsd;*.dtd),'
                   'XML/JSP TagLib Files (*.tld;*.xml),'
                   'Objective-C (*.m;*.mm;*.h),'
                   'IDL Files (*.idl),'
                   'Ada Files (*.ada;*.adb;*.ads),'
                   'Applescript Files (*.applescript),'
                   'Basic Files (*.vb;*.vbs;*.bas;*.frm),'
                   'Cobol Files (*.cob;*.cbl;*.ocb),'
                   'JCL Files (*.jcl),'
                   'JavaScript (*.js;*.ds),'
                   'ActionScript (*.as),'
                   'Pascal Files (*.pas;*.dpr),'
                   'Fortran Files (*.for;*.f),'
                   'PL/I Files (*.pl1),'
                   'InstallScript (*.rul),'
                   'Perl Files (*.pl;*.pm;*.perl;*.plx),'
                   'Python Files (*.py),'
                   'Ruby Files (*.rb;*.rby),'
                   'Java Properties (*.properties),'
                   'Lua Files (*.lua),'
                   'Tcl Files (*.tcl;*.tlib;*.itk;*.itcl;*.exp),'
                   'PV-WAVE (*.pro),'
                   'Slick-C (*.e;*.sh),'
                   'SQL Files (*.sql),'
                   'SAS Files (*.sas),'
                   'Text Files (*.txt),'
                   'Verilog Files (*.v),'
                   'VHDL Files (*.vhd),'
                   'SystemVerilog Files (*.sv;*.svh;*.svi),'
                   'Vera Files (*.vr;*.vrh),'
                   'Erlang Files (*.erl;*.hrl),'
                   ;

    def_updown_col=0;                   /* cursor movement */
    def_cursorwrap=0;                   /* ditto. */
    def_click_past_end=1;               /* ditto */
    def_start_on_first=1;               /* vs A B C; view A. */
    def_vc_system='Subversion'          /* svn is default version control */
#if __VERSION__ >= 16.0
    def_auto_unsurround_block=0;        /* Delete line, not block. */
#endif

    /* Make it grok:  # include <stuff.h> */
    for (i = 0; i < aCLikeIncs._length(); i++)
        replace_def_data("def-":+aCLikeIncs[i]:+"-include",
                         '^[ \t]*(\#[ \t]*include|include|\#[ \t]*line)[ \t]#({#1:i}[ \t]#|)(<{#0[~>]#}>|"{#0[~"]#}")');
    replace_def_data("def-m-include", '^[ \t]*(\#[ \t]*include|\#[ \t]*import|include|\#[ \t]*line)[ \t]#({#1:i}[ \t]#|)(<{#0[~>]#}>|"{#0[~"]#}")');
    replace_def_data("def-e-include", '^[ \t]*(\#[ \t]*include|\#[ \t]*import|\#[ \t]*require|include)[ \t]#(''{#0[~'']#}''|"{#0[~"]#}")');

    /* Replace the default unicode proportional font with the fixed oned. */
    _str sCodeFont = _default_font(CFG_SBCS_DBCS_SOURCE_WINDOW);
    _str sUnicodeFont = _default_font(CFG_UNICODE_SOURCE_WINDOW);
    if (pos("Default Unicode", sUnicodeFont) > 0 && length(sCodeFont) > 5)
        _default_font(CFG_UNICODE_SOURCE_WINDOW,sCodeFont);
    if (machine()=='INTELSOLARIS' || machine()=='SPARCSOLARIS')
    {
        _default_font(CFG_MENU,'DejaVu Sans,10,0,0,');
        _default_font(CFG_DIALOG,'DejaVu Sans,10,0,,');
        _ConfigEnvVar('VSLICKDIALOGFONT','DejaVu Sans,10,0,,');
    }

    /* Not so important. */
    int fSearch = 0x400400; /* VSSEARCHFLAG_WRAP | VSSEARCHFLAG_PROMPT_WRAP */;
    _default_option('S', (_str)fSearch);


#if __VERSION__ >= 17.0
    /*
     * Language settings via API.
     */
    _str sLangId;
    foreach (sLangId in aMyLangIds)
    {
        LanguageSettings.setIndentCaseFromSwitch(sLangId,    true);
        LanguageSettings.setBeginEndStyle(sLangId,           BES_BEGIN_END_STYLE_2);
        LanguageSettings.setIndentWithTabs(sLangId,          false);
        LanguageSettings.setUseAdaptiveFormatting(sLangId,   true);
        LanguageSettings.setSaveStripTrailingSpaces(sLangId, STSO_STRIP_MODIFIED);

        /* C/C++ setup, wrap at column 80 not 64. */
        sTmp = LanguageSettings.getCommentWrapOptions(sLangId);
        if (length(sTmp) > 10)
        {
            typeless ntBlockCommentWrap, ntDocCommentWrap, ntFixedWidth;
            parse sTmp with ntBlockCommentWrap ntDocCommentWrap nt3 nt4 nt5 ntFixedWidth sRest;
            if ((int)ntFixedWidth < 80)
                LanguageSettings.setCommentWrapOptions('c', ntBlockCommentWrap:+' ':+ntDocCommentWrap:+' ':+nt3:+' ':+nt4:+' ':+nt5:+' 80 ':+sRest);
            //replace_def_data("def-comment-wrap-c",'0 1 0 1 1 64 0 0 80 0 80 0 80 0 0 1 '); - default
            //replace_def_data("def-comment-wrap-c",'0 1 0 1 1 80 0 0 80 0 80 0 80 0 0 0 '); - disabled
            //replace_def_data("def-comment-wrap-c",'1 1 0 1 1 80 0 0 80 0 80 0 80 0 0 1 '); - enable block comment wrap.
        }
    }
    LanguageSettings.setIndentWithTabs('mak', true);
    LanguageSettings.setLexerName('mak', 'kmk');

    /* Fix .asm and add .mac and .kmk. */
    replace_def_data("def-lang-for-ext-asm",'masm');
    replace_def_data("def-lang-for-ext-mac",'masm');
    replace_def_data("def-lang-for-ext-kmk",'mak');
#endif

    /** @todo
     *  def_save_options
     *   */

    message("Please restart SlickEdit.")
}

/**
 * Module initiation.
 */
definit()
{
    /* do cleanup. */
    for (i = 0; i < 999; i++)
    {
        index = name_match("def-koptions-", 1 /*find_first*/, MISC_TYPE);
        if (!index)
            break;
        delete_name(index);
    }

    /* do init */
    k_styles_create();
    k_menu_create();
    iTimer = _set_timer(1000, k_menu_create, "timer");
    /* createMyColorSchemeAndUseIt();*/
}
