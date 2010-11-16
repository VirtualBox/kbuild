/* $Id: kkeys.e 2428 2010-11-16 14:47:04Z knut.osmundsen@oracle.com $ */
/** @file
 * Bird's key additions to Visual Slickedit.
 */

/*
 * Copyright (c) 2004-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

/*******************************************************************************
*  Header Files                                                                *
*******************************************************************************/
#include 'slick.sh'


/*******************************************************************************
*  Global Variables                                                            *
*******************************************************************************/
defeventtab default_keys
def  'A-UP'     = find_prev
def  'A-DOWN'   = find_next
def  'A-PGUP'   = prev_proc
def  'A-PGDN'   = next_proc
def  'A-d'      = delete_line
def  'A-o'      = kkeys_duplicate_line
def  'A-s'      = kkeys_switch_lines
def  'A-u'      = undo_cursor           /* will cursor movement in one undo step. */
def  'A-g'      = goto_line
def  'A-z'      = kkeys_fullscreen
def  'INS'      = boxer_paste
def  'S-INS'    = insert_toggle
def  'C-UP'     = kkeys_scroll_down
def  'C-DOWN'   = kkeys_scroll_up
def  'C-PGUP'   = prev_window
def  'C-PGDN'   = next_window
def  'C-DEL'    = kkeys_delete_right
#if __VERSION__ >= 15.0
def  'S-C-='    = svn_diff_with_base
#endif
#if __VERSION__ >= 14.0
def  'C-/'      = kkeys_push_ref
def  'S-C-/'    = push_ref
def  'S-A-]'    = next_buff_tab
def  'S-A-['    = prev_buff_tab
def  'S-A-U'    = kkeys_gen_uuid
#endif
/* For the mac (A/M mix, all except A-z): */
def  'M-1'      = cursor_error
def  'M-UP'     = find_prev
def  'M-DOWN'   = find_next
def  'M-PGUP'   = prev_proc
def  'M-PGDN'   = next_proc
def  'M-d'      = delete_line
def  'M-f'      = kkeys_open_file_menu
def  'M-e'      = kkeys_open_edit_menu
def  'M-o'      = kkeys_duplicate_line
def  'M-s'      = kkeys_switch_lines
def  'M-t'      = kkeys_open_tools_menu
def  'M-u'      = undo_cursor
def  'M-g'      = goto_line
#if __VERSION__ >= 14.0
def  'S-M-]'    = next_buff_tab
def  'S-M-['    = prev_buff_tab
def  'S-M-U'    = kkeys_gen_uuid
#endif
/* Fixing brainfucked slickedit silliness: */
def  'M-v'      = paste


/** Saves the cursor position. */
static long kkeys_save_cur_pos()
{
   long offset = _QROffset();
   message(offset);
   return offset;
}

/** Restores a saved cursor position. */
static void kkeys_restore_cur_pos(long lSavedCurPos)
{
   _GoToROffset(lSavedCurPos);
}


_command kkeys_switch_lines()
{
   /* Allocate a selection for copying the current line. */
   cursor_down();
   mark_id= _alloc_selection();
   if (mark_id>=0)
   {
      _select_line(mark_id);
      cursor_up();
      cursor_up();
      _move_to_cursor(mark_id);
      cursor_down();
      _free_selection(mark_id);
      // This selection can be freed because it is not the active selection.
   }
   else
      message(get_message(mark_id));
}

_command kkeys_duplicate_line()
{
   /* Allocate a selection for copying the current line. */
   mark_id= _alloc_selection();
   if (mark_id>=0)
   {
      _select_line(mark_id);
      _copy_to_cursor(mark_id);
      // This selection can be freed because it is not the active selection.
      _free_selection(mark_id);
      cursor_down();
   }
   else
       message(get_message(mark_id));
}

_command kkeys_delete_right()
{
   col=p_col
   search('[ \t]#|?|$|^','r+');
   if ( match_length()&& get_text(1,match_length('s'))=='' )
   {
      _nrseek(match_length('s'));
      _delete_text(match_length());
   }
   else
      delete_word();
   p_col=col
   //retrieve_command_results()

}

_command kkeys_delete_left()
{
   //denne virker ikkje som den skal!!!
   message "not implemented"
/*
   return;
   col=p_col
   search('[ \t]#|?|$|^','r-');
   if ( match_length()&& get_text(1,match_length('s'))=='' )
   {
      _nrseek(match_length('s'));
      _delete_text(match_length());
   }
   else
      delete_word();
   p_col=col
*/
}

_command kkeys_scroll_up()
{
   if (p_cursor_y == 0)
      down();
   set_scroll_pos(p_left_edge, p_cursor_y-1);
}

_command kkeys_scroll_down()
{
   if (p_cursor_y intdiv p_font_height == p_char_height-1)
      up()
   set_scroll_pos(p_left_edge, p_cursor_y+p_font_height);
}

_command boxer_paste()
{
   long lSavedCurPos = kkeys_save_cur_pos()
   paste();
   kkeys_restore_cur_pos(lSavedCurPos);
}

_command kkeys_fullscreen()
{
    fullscreen();
}


/* for later, not used yet. */

_command boxer_select()
{
   if (command_state())
      fSelected = (p_sel_length != 0);
   else
      fSelected = select_active();

   key = last_event();
   if (key :== name2event('s-down'))
   {
      if (!fSelected)
         select_line();
      else
         cursor_down();
   }
   else if (key :== name2event('s-up'))
   {
      if (!fSelected)
         select_line();
      else
         cursor_up();
   }
   else if (key :== name2event('s-left'))
   {
      if (!fSelected)
         select_char();
      else
         cursor_left();
   }
   else if (key :== name2event('s-right'))
   {
      if (!fSelected)
         select_char();
      else
         cursor_right();
   }
   else if (key :== name2event('s-home'))
   {
      if (!fSelected) select_char();
      begin_line_text_toggle();
   }
   else if (key :== name2event('s-end'))
   {
      if (!fSelected) select_char();
      end_line();
      if (p_col > 0) //this is not identical with boxer...
         cursor_left();
   }
   else if (key :== name2event('c-s-home'))
   {
      if (!fSelected) select_char();
      top_of_buffer();
   }
   else if (key :== name2event('c-s-end'))
   {
      if (!fSelected) select_char();
      bottom_of_buffer();
   }
   else if (key :== name2event('c-s-left'))
   {
      if (!fSelected)
      {
         cursor_left();
         select_char(); /* start this selection non-inclusive */
      }
      prev_word();
   }
   else if (key :== name2event('c-s-right'))
   {
      if (!fSelected)
      {
         select_char(); /* start this selection non-inclusive */
      }
      /* temporary hack */
      prevpos = p_col;
      prevline = p_line;
      p_col++;
      next_word();
      if ((p_line == prevline && p_col > prevpos + 1) || (p_line != prevline && p_col > 0))
         p_col--;
   }
}

#if __VERSION__ >= 14.0

/**
 * Search for references only in the current workspace.
 */
_command kkeys_push_ref()
{
   if (_isEditorCtl())
   {
      sProjTagFile = project_tags_filename();
      sLangId      = p_LangId;
      if (sProjTagFile != '')
      {

         /* HACK ALERT: Make sure gtag_filelist_last_ext has the right value. */
         _update_tag_filelist_ext(sLangId);

         /* save */
         boolean saved_gtag_filelist_cache_updated = gtag_filelist_cache_updated;
         _str    saved_gtag_filelist_ext[]         = gtag_filelist_ext;

         /* HACK ALERT: Replace the tag file list for this language. */
         gtag_filelist_ext._makeempty();
         gtag_filelist_ext[0] = sProjTagFile;
         saved_gtag_filelist_cache_updated = true;

         /* Do the reference searching. */
         push_ref('-e ' :+ sLangId);

         /* restore*/
         gtag_filelist_cache_updated = saved_gtag_filelist_cache_updated;
         gtag_filelist_ext           = saved_gtag_filelist_ext;
      }
      else
         push_ref();
   }
   else
      push_ref();
}


_command kkeys_gen_uuid()
{
   _str uuid = guid_create_string('G');
   uuid = lowcase(uuid);

   long lSavedCurPos = kkeys_save_cur_pos();
   _insert_text(uuid);
   kkeys_restore_cur_pos(lSavedCurPos);
}

#endif /* >= 14.0 */

/** @name Mac OS X Hacks: Alt+[fet] -> drop down menu
 *
 * This only works when the alt menu hotkeys are enabled in the
 * settings.  Al
 *
 * @{
 */
_command void kkeys_open_file_menu()
{
   call_key(A_F)
}

_command void kkeys_open_edit_menu()
{
   call_key(A_E)
}

_command void kkeys_open_tools_menu()
{
   call_key(A_T)
}
/** @} */

void nop()
{

}


#if __VERSION__ >= 14.0

/*
 * Some diff keyboard hacks for Mac OS X.
 */
defeventtab _diff_form
def  'M-f'      = kkeys_diffedit_find
def  'M-n'      = kkeys_diffedit_next
def  'M-p'      = kkeys_diffedit_prev

_command kkeys_diffedit_find()
{
   _nocheck _control _ctlfind;
   _ctlfind.call_event(_ctlfind, LBUTTON_UP);
}

_command kkeys_diffedit_next()
{
   _nocheck _control _ctlfile1;
   _nocheck _control _ctlfile2;
   _DiffNextDifference(_ctlfile1, _ctlfile2);
}

_command kkeys_diffedit_prev()
{
   _nocheck _control _ctlfile1;
   _nocheck _control _ctlfile2;
   _DiffNextDifference(_ctlfile1, _ctlfile2, '-');
}

#endif /* >= 14.0 */

