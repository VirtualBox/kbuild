/* $Id: kBuilddocs.c 785 2007-01-24 22:21:56Z knut.osmundsen@oracle.com $ */
/** @file
 * A kBuild Documentation file with the sole purpose of giving doxygen
 * something to chew on.
 */

/*
THIS IS MOSTLY OBSOLETE AND INCORRECT
THIS IS MOSTLY OBSOLETE AND INCORRECT
THIS IS MOSTLY OBSOLETE AND INCORRECT
THIS IS MOSTLY OBSOLETE AND INCORRECT
THIS IS MOSTLY OBSOLETE AND INCORRECT
THIS IS MOSTLY OBSOLETE AND INCORRECT
THIS IS MOSTLY OBSOLETE AND INCORRECT
THIS IS MOSTLY OBSOLETE AND INCORRECT
THIS IS MOSTLY OBSOLETE AND INCORRECT
THIS IS MOSTLY OBSOLETE AND INCORRECT
*/

/** @mainpage                   kBuild
 *
 * @section     kBuild_intro            Introduction
 *
 * kBuild is a build system which intention is to simplify your makefiles
 * and to hide cross platform detail for projects.
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
 * files are named on the form 'tool.[toolname].kMk'.
 *
 * The tool configuration contains callable macros and definitions for
 * invoking the tool. This process involes setting up required environment
 * variables, interpreting symblic flags, wrap setting up define and include
 * arguments (_DEFS, _INCL), executing the tool, and cleaning up.
 *
 *
 *
 * @section     kBuild_makeref          Makefile Reference
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
 *
 * @subsection  kBuild_attributes   Target Attributes
 *
 * Target attributes are use to define how to build a target. Some attributes
 * have several specializations, like the .INCS attribute, a rule of thum is
 * that the more specialized attributes is given higher precedence when
 * ordering the result on a commandline or when used internally by kBuild.
 * For instance, concidering the gcc C compiler, kBuild will pass .CINCS
 * before .INCS.
 *
 * kBuild defines a wide range of target attributes. They might apply
 * differently to different main targets. See in the section for the main in
 * question for details on which attributes applicable to targets of that type.
 *
 * The attributes can be used on levels with different scope:
 *
 *      - The broadest scope is gained when using the attribute unqualified,
 *        applying it to all targets in that makefile.
 *
 *      - Qualifying the attribute with a target name, for instance like
 *        hello.CDEFS, applies that attribute to a target and all it's
 *        dependencies. The target name does not have to be a main target,
 *        allthough for only the main targets may be used without care to
 *        tool or platform specific suffixes or prefixes.
 *
 *      - Qualifying the attribute with a target name and a immediate
 *        dependant make that attribute apply to that dependant when
 *        made for that specific main target.
 *
 *
 * Possible target attributes:
 * <dl>
 *  <dt>.FLAGS
 *      <dd>Flags to pass to all tools. The flags are passed unchanged.
 *
 *  <dt>.CFLAGS
 *      <dd>Flags to pass to the compiler of C source files.
 *
 *  <dt>.CXXFLAGS
 *      <dd>Flags to pass to the compiler of C++ source files.
 *
 *  <dt>.AFLAGS
 *      <dd>Flags to pass to the compiler of assembly source files.
 *
 *  <dt>.JFLAGS
 *      <dd>Flags to pass to the compiler of java source files.
 *
 *  <dt>.RCFLAGS
 *      <dd>Flags to pass to the compiler of resource source files.
 *
 *  <dt>.HLPFLAGS
 *      <dd>Flags to pass to the compiler of help source files.
 *
 *  <dt>.IPFFLAGS
 *      <dd>Flags to pass to the compiler of book (OS/2 IPF) source files.
 *
 *  <dt>.LFLAGS
 *      <dd>Flags to pass to the linker.
 *
 *  <dt>.ARFLAGS
 *      <dd>Flags to pass to the librarian.
 *
 *
 *  <dt>.OPTS
 *      <dd>Symbolic options passed to compilers and linkers. kBuild defines
 *          (or will soon) a set of generic options used among all the tools
 *          which can be used for better cross tool interoperability.
 *
 *          Note that allthough generic symbolic options are a nice idea, tools
 *          have different capabilities, so only a very small subset might be
 *          valid for all tools or all tools in a tools group. kBuild will
 *          detect illegal options and complain about it.
 *
 *  <dt>.COPTS
 *      <dd>Symbolic options passed to the compiler of C source files.
 *
 *  <dt>.CXXOPTS
 *      <dd>Symbolic options passed to the compiler of C++ source files.
 *
 *  <dt>.AOPTS
 *      <dd>Symbolic options passed to the compiler of assmebly source files.
 *
 *  <dt>.JOPTS
 *      <dd>Symbolic options passed to the compiler of java source files.
 *
 *  <dt>.RCOPTS
 *      <dd>Symbolic options passed to the compiler of resource source files.
 *
 *  <dt>.HLPOPTS
 *      <dd>Symbolic options passed to the compiler of help source files.
 *
 *  <dt>.IPFOPTS
 *      <dd>Symbolic options passed to the compiler of book (OS/2 IPF) source files.
 *
 *  <dt>.LOPTS
 *      <dd>Symbolic options passed to the linker.
 *
 *  <dt>.AROPTS
 *      <dd>Symbolic options passed to the librarian.
 *
 *
 *  <dt>.DEFS
 *      <dd>Definitions to pass to compilers. The attribute contains a list
 *          of definitions with no -D or any other compiler option in front.
 *          If the definition have an value assigned use follow it by and
 *          equal sign and the value, no spaces before or after the equal sign.
 *
 *  <dt>.CDEFS
 *      <dd>Definitions to pass to the compiler of C source files.
 *
 *  <dt>.CXXDEFS
 *      <dd>Definitions to pass to the compiler of C++ source files.
 *
 *  <dt>.ADEFS
 *      <dd>Definitions to pass to the compiler of assmbly source files.
 *
 *  <dt>.RCDEFS
 *      <dd>Definitions to pass to the compiler of resource source files.
 *
 *
 *  <dt>.INCS
 *      <dd>Include path directives passed to compilers. The attribute contains
 *          a list of paths to directory which are to be searched for included
 *          files. The paths shall not be prefixed with any compiler options
 *          like -I, neither shall they be separated with ':' or ';'. Use space
 *          as separator between paths.
 *
 *  <dt>.CINCS
 *      <dd>Include path directives to pass to the compiler of C source files.
 *
 *  <dt>.CXXINCS
 *      <dd>Include path directives to pass to the compiler of C++ source files.
 *
 *  <dt>.AINCS
 *      <dd>Include path directives to pass to the compiler of assmbly source files.
 *
 *  <dt>.RCINCS
 *      <dd>Include path directives to pass to the compiler of resource source files.
 *
 *
 *  <dt>.INS
 *      <dd>For targets which are by default private defining this attribute
 *          cause that target to be installed.
 *
 *          Note! This attribute is not automatically inherited by subtargets,
 *          i.e. use on makefile scope means it applies to main targets, while
 *          using it on a target scope means that specific target and nothing
 *          else.
 *
 *  <dt>.PUB
 *      <dd>For targets which are by default private defining this attribute
 *          cause that target to be published.
 *          Note! Same as .INS note.
 *
 *  <dt>.PRIVATE
 *      <dd>For targets which are by default published and/or installed
 *          defining this attribute will prevent the target from being
 *          installed and publish. This attribute overrides .INS and .PUB.
 *          Note! Same as .INS note.
 *
 *
 *  <dt>.INSDIR
 *      <dd>Which subdirectory under the installation root to put the target.
 *
 *  <dt>.PUBDIR
 *      <dd>Which subdirectory under the publish root to put the target.
 *
 *
 *  <dt>.NAME
 *      <dd>Alternative name under which to publish and install the target.
 *          Note! Same as .INS note.
 *
 *  <dt>.SUFF
 *      <dd>Suffix to append to the target name. Most subtargets have a default
 *          suffix and also some main targets do. This attribute set or
 *          overrides suffix of a target.
 *          Note! Same as .INS note.
 *          Note! Suffix differs from extension in that it includes any leading
 *          dot.
 *
 *  <dt>.PREF
 *      <dd>Prefix to append to the target name. Some targets (libraries for
 *          instance) have a default prefix. This attribute sets or overrides
 *          the prefix.
 *          Note! Same as .INS note.
 *
 *
 *  <dt>.CFG
 *      <dd>The configuration to use when building the target. It is
 *          mandatory that this attribute is present.
 *
 * </dl>
 *
 *
 *
 * @subsection  kBuild_makerefclues     Main Target Clues
 *
 * The main target clues are what tells kBuild what to do. This section will
 * detail with each of them in some detail.
 *
 *
 * @subsubsection  kBuild_PROGRAMS      The PROGRAMS Clue
 *
 * The PROGRAMS clue gives kBuild a list of executable image targets to be made.
 *
 *
 * @subsubsection  kBuild_DLLS          The DLLS Clue
 *
 * The DLLS clue gives kBuild a list of dynamic link library and shared object
 * library targets to be made
 *
 *
 * @subsubsection  kBuild_DRIVERS       The DRIVERS Clue
 *
 * The DRIVERS clue gives kBuild a list of driver module targets (OS kernel
 * modules) to be made.
 *
 *
 * @subsubsection  kBuild_LIBRARIES     The LIBRARIES Clue
 *
 * The LIBRARIES clue gives kBuild a list of object library targets to be made.
 *
 *
 * @subsubsection  kBuild_OBJECTS       The OBJECTS Clue
 *
 * The OBJECTS clue gives kBuild a list of object module targets to be made.
 *
 *
 * @subsubsection  kBuild_JARS          The JARS Clue
 *
 * The JARS clue gives kBuild a list of java archive targets to be made.
 *
 *
 * @subsubsection  kBuild_CLASSES       The CLASSES Clue
 *
 * The CLASSES clue gives kBuild a list of java class targets to be made.
 *
 *
 * @subsubsection  kBuild_OTHERS        The OTHERS Clue
 *
 * The OTHERS clue gives kBuild a list of other targets to be made.
 *
 *
 *
 */

