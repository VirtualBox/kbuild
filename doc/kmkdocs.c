/* $Id: kmkdocs.c 19 2002-10-17 23:02:18Z knut.osmundsen@oracle.com $
 *
 * kmk design and documentation.
 *
 * Copyright (c) 2002 knut st. osmundsen <bird@anduin.net>
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/** @design             kmk
 *
 * kmk is the make file interpreter and executer in kBuild. It is based on
 * FreeBSD make but some changes have been made.
 *
 *
 *
 * @subsection          Differences From BSD Make
 *
 * The main difference is that kmk doesn't make use of an external shell to execute
 * commands. It uses it's internal micro shell. This has a number of advantages
 * when it comes to portability. It also speeds up the execution a little bit.
 *
 * The other important difference is default makefile names and order. The names
 * and order is as follows:
 * <ol>
 *     <li>makefile.kmk
 *     <li>Makefile.kmk
 *     <li>makefile
 *     <li>Makefile
 * </ol>
 *
 *
 *
 * @subsection          The Micro Shell
 *
 * The micro shell provides the basic shell functionality kBuild need - no more,
 * no less. It is intended to be as simple as possible.
 *
 * The shell commands are case sensitive - all lowercase.
 *
 * The shell environment variables are case sensitive or insensitive according to
 * host os.
 *
 *
 *
 * @subsubsection       Command Separators
 *
 * There is one command separator '&&'. This works like splitting the command line
 * into several makefile lines. This splitting isn't done by the micro shell but
 * the makefile interpreter.
 *
 * You might thing this is limiting, but no, you can use all the makefile command
 * prefixes.
 *
 *
 *
 * @subsubsection       Path Component Separator (/)
 *
 * The shell uses '/' as path component separator.
 * For host OSes  with the notion of drive letters or similar, ':' is
 * used to separate the drive letter and the path.
 *
 *
 *
 * @subsubsection       UNC paths
 *
 * For host OSes which supports UNC paths these are supported but for the chdir
 * command.
 *
 * The Path Component Separator is still '/' for UNC paths.
 *
 *
 *
 * @subsubsection       Wildchars
 *
 * '*' and '?' are accepted as wildchars.
 *
 * '*' means 0 or more characters. <br>
 * '?' means 1 character.
 *
 * When the term 'pattern' is use in command description this means that
 * wildchars are accepted.
 *
 *
 *
 * @subsubsection       Quoting
 *
 * Use double quotes (") to quote filenames or executables containing spaces.
 *
 *
 *
 * @subsubsection       Execute Program
 *
 * If the first, possibly quoted, word of a commandline if not found as an
 * internal command will be tried executed. If no path it will be searched
 * for in the PATH environment variable.
 *
 *
 *
 * @subsubsection       Commands
 *
 * This section will describe the commands implemented by the shell.
 *
 *
 *
 * @subsubsubsection    copy
 * Copies one or more files to a target file or directory.
 *
 * <b>Syntax: copy <source file pattern> [more sources] <target> </b>
 *
 * Specify one or more source file patterns.
 *
 * Specify exactly one target. The target may be a directory or a file.
 * If it's a file and multiple source files specified either thru pattern or
 * multiple source file specifications, the target file will be a copy of the
 * last one.
 *
 * The command fails if a source file isn't found. It also fails on read or
 * write errors.
 *
 *
 *
 * @subsubsubsection    copytree
 * Copies one or more files to a target file or directory.
 *
 * <b>Syntax: copytree <source directory> <target directory> </b>
 *
 * Specify exactly one source directory.
 *
 * Specify exactly one target directory. The target directory path will be
 * created if doesn't exist.
 *
 * The command fails if source directory isn't found. It also fails on read or
 * write errors.
 *
 *
 *
 * @subsubsubsection    rm
 * Deletes one or more files.
 *
 * <b>Syntax: rm [file pattern] [more files] </b>
 *
 * Specify 0 or more file patterns for deletion.
 *
 * This command fails if it cannot delete a file. It will not fail if a file
 * doesn't exist. It will neither fail if no files are specified.
 *
 *
 *
 * @subsubsubsection    rmtree
 * Deletes one or more directory trees.
 *
 * <b>Syntax: rmtree [directory pattern] [directories] </b>
 *
 * Specify 0 or more directory patterns for deletion.
 *
 * This command fails if it cannot delete a file or directory. It will not fail
 * if a directory doesn't exist. It will neither fail if no files are specified.
 *
 *
 *
 * @subsubsubsection    chdir
 * Changes the current directory.
 *
 * This updates the .CWD macro to the new current directory path.
 *
 * <b>Syntax: chdir <directory> </b>
 *
 *
 *
 * @subsubsubsection    mkdir
 * Create directory.
 *
 * <b>Syntax:  mkdir <directory> </b>
 *
 * Specify one directory to create.
 *
 *
 *
 * @subsubsubsection    rmdir
 * Remove directory.
 *
 * <b>Syntax: rmdir <directory> </b>
 *
 * Specify one directory to remove. The directory must be empty.
 *
 * This command failes if directory isn't empty. It will not fail if
 * the directory doesn't exist.
 *
 *
 *
 * @subsubsubsection    set
 * Set environment variable.
 *
 * <b>Syntax: set <envvar>=<value> </b>
 *
 *
 *
 * @subsubsubsection    unset
 * Unset enviornment variable(s).
 *
 * <b>Syntax: unset <envvar pattern> [more envvars] </b>
 *
 * Specify on or more environment variable patterns.
 *
 *
 *
 * @subsubsubsection    pushenv
 * Pushes a set of environment variables onto the environment stack. The
 * variables can later be popped back using the popenv command.
 *
 * If '*' is specified as pattern the complete enviornment is pushed and
 * when popped it will <b>replace</b> the enviornment.
 *
 * <b>Syntax: pushenv <envvar pattern> [more envvars] </b>
 * <b>Syntax: pushenv * </b>
 *
 *
 *
 * @subsubsubsection    popenv
 * Pop a set of environment variables from the environment stack. If a '*'
 * push was done, we'll replace the enviornment with the variables poped off
 * the stack.
 *
 * <b>Syntax: popenv </b>
 *
 *
 *
 * @subsubsubsection    echo
 * Prints a message to stdout.
 *
 * <b>Syntax: echo <level> <message>
 *
 * Level is verbosity level of the message. This is compared with the
 * KBUILD_MSG_LEVEL environment variable. The message is suppressed if the
 * level is lower that KBUILD_MSG_LEVEL.
 *
 * The message can be empty. Then a blank line will be printed.
 *
 *
 *
 */
