/* $Id: kBuilddocs.c 59 2003-12-13 19:14:23Z knut.osmundsen@oracle.com $ */
/** @file
 * kBuild Documentation file with the sole purpose of giving doxygen
 * something to chew on.
 */


/** @page       kBuild          kBuild
 *
 * @section     kBuild_intro    Introduction
 *
 * kBuild is a build system which intention is to simplify your makefiles
 * and to hide cross platform projects issues.
 *
 * kBuild is layered in three tiers, first comes the instructions given in the
 * makefile, seconds comes the instructions given in a configuration (CFG),
 * third comes the tool configuration. Now, let me explain by giving an
 * example - the typical hello world program.
 *
 * In the makefile you tell kBuild that there is one program called 'hello'
 * which shall be built by defining PROGRAMS=hello. This 'hello' target
 * have one source file 'hello.c', thus hello.SRCS=hello.c. For enabling the
 * compile time fireworks option WITH_FIREWORKS needs to be #defined, this is
 * acomplished setting hello_DEFS=WITH_FIREWORKS. The fireworks requires the
 * libfirecracker.a library, hello_LIBS=firecracker. hello_CFG=gcc.default
 * tells kBuild that the 'hello' program target is to build using the
 * configuration called 'gcc.default'.
 *
 * The configuration 'gcc.default' can be defined anywhere, but if it's not
 * defined yet kBuild will load it from the file 'cfg.gcc.default.kMk'.
 * The configuration contains definitions of how to build all kind of files
 * and references to the tools to be used and such. The configuration will
 * ask kBuild to load any tool configurations it needs. These configuration
 * files are named on the form 'tool.<toolname>.kMk'.
 *
 * The tool configuration contains callable macros and definitions for
 * invoking the tool. This process involes setting up required environment
 * variables, interpreting symblic flags, wrap setting up define and include
 * arguments (_DEFS, _INCL), executing the tool, and cleaning up.
 *
 *
 *
 * @section     kBuild_makeref  Makefile Reference
 *
 * The make file must start with including a configuration file which sets up
 * the inital kBuild environment with all it's standard variables. It's
 * prefered that you keep one or more include files for a project which figures
 * out stuff like the root location and includes the kBuild header.kmk file.
 * For the main config file of a source tree the prefered name is kBuild.kMk.
 *
 * After having included the kBuild environment and any additions to it
 * specific to the source tree (like predefined configurations) setup the main
 * target clues. The main target clues are what kBuild uses to determin what to
 * build and how those things are to be built. The clues are put into the
 * variables PROGRAMS, DLLS, DRIVERS, LIBRARIES, OBJECTS, JARS, CLASSES and
 * OTHERS. The variables and the attributes of their targets will be explained
 * in detail later.
 *
 * Giving kBuild it's main clues, set the attributes of each of the targets.
 *
 * When all target attributes have been set include the kBuild main rules. Do
 * this by specificly address the include: include $(PATH_KBUILD)/rules.kMk
 *
 * If there is need for any rules or your own, put them at the end of the
 * makefile. Target rules may be used to generate files, or to do special task
 * during custom passes, or make special targets specified in the OTHERS clue.
 *
 *
 */

