dnl Macros used to build the Wine configure script
dnl
dnl Copyright 2002 Alexandre Julliard
dnl
dnl This library is free software; you can redistribute it and/or
dnl modify it under the terms of the GNU Lesser General Public
dnl License as published by the Free Software Foundation; either
dnl version 2.1 of the License, or (at your option) any later version.
dnl
dnl This library is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
dnl Lesser General Public License for more details.
dnl
dnl You should have received a copy of the GNU Lesser General Public
dnl License along with this library; if not, write to the Free Software
dnl Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
dnl
dnl As a special exception to the GNU Lesser General Public License,
dnl if you distribute this file as part of a program that contains a
dnl configuration script generated by Autoconf, you may include it
dnl under the same distribution terms that you use for the rest of
dnl that program.

dnl WINE_CHECK_HOST_TOOL(VARIABLE, PROG-TO-CHECK-FOR, [VALUE-IF-NOT-FOUND], [PATH])
dnl
dnl Like AC_CHECK_TOOL but without the broken fallback to non-prefixed name
dnl
AC_DEFUN([WINE_CHECK_HOST_TOOL],
[AS_VAR_SET_IF([ac_tool_prefix],
  [AC_CHECK_PROG([$1],[${ac_tool_prefix}$2],[${ac_tool_prefix}$2],,[$4])])
AS_VAR_IF([ac_cv_prog_$1],[],
  [AS_VAR_IF([cross_compiling],[yes],[],
    [AS_UNSET([ac_cv_prog_$1])
     AC_CHECK_PROG([$1],[$2],[$2],[$3],[$4])])],
[AS_VAR_COPY([$1],[ac_cv_prog_$1])])])

dnl **** Initialize the programs used by other checks ****
dnl
dnl Usage: WINE_PATH_SONAME_TOOLS
dnl Usage: WINE_PATH_PKG_CONFIG
dnl
AC_DEFUN([WINE_PATH_SONAME_TOOLS],
[AC_PATH_PROG(LDD,ldd,true,/sbin:/usr/sbin:$PATH)
AC_CHECK_TOOL(OTOOL,otool,otool)
AC_CHECK_TOOL(READELF,[readelf],true)])

AC_DEFUN([WINE_PATH_PKG_CONFIG],
[WINE_CHECK_HOST_TOOL(PKG_CONFIG,[pkg-config])])

AC_DEFUN([WINE_PATH_MINGW_PKG_CONFIG],
[AS_VAR_IF([HOST_ARCH],[i386],
           [ac_prefix_list="m4_foreach([ac_wine_cpu],[i686,i586,i486,i386],[ac_wine_cpu-w64-mingw32-pkg-config ])"],
           [ac_prefix_list="$host_cpu-w64-mingw32-pkg-config"])
AC_CHECK_PROGS(MINGW_PKG_CONFIG,[$ac_prefix_list],false)])

dnl **** Extract the soname of a library ****
dnl
dnl Usage: WINE_CHECK_SONAME(library, function, [action-if-found, [action-if-not-found, [other_libraries, [pattern]]]])
dnl
AC_DEFUN([WINE_CHECK_SONAME],
[AC_REQUIRE([WINE_PATH_SONAME_TOOLS])dnl
AS_VAR_PUSHDEF([ac_Lib],[ac_cv_lib_soname_$1])dnl
m4_pushdef([ac_lib_pattern],m4_default([$6],[lib$1]))dnl
AC_MSG_CHECKING([for -l$1])
AC_CACHE_VAL(ac_Lib,
[ac_check_soname_save_LIBS=$LIBS
LIBS="-l$1 $5 $LIBS"
  AC_LINK_IFELSE([AC_LANG_CALL([], [$2])],
  [case "$LIBEXT" in
    dll) AS_VAR_SET(ac_Lib,[`$ac_cv_path_LDD conftest.exe | grep "$1" | sed -e "s/dll.*/dll/"';2,$d'`]) ;;
    dylib) AS_VAR_SET(ac_Lib,[`$OTOOL -L conftest$ac_exeext | grep "ac_lib_pattern\\.[[0-9A-Za-z.]]*dylib" | sed -e "s/^.*\/\(ac_lib_pattern\.[[0-9A-Za-z.]]*dylib\).*$/\1/"';2,$d'`]) ;;
    *) AS_VAR_SET(ac_Lib,[`$READELF -d conftest$ac_exeext | grep "NEEDED.*ac_lib_pattern\\.$LIBEXT" | sed -e "s/^.*\\m4_dquote(\\(ac_lib_pattern\\.$LIBEXT[[^	 ]]*\\)\\).*$/\1/"';2,$d'`])
       AS_VAR_IF([ac_Lib],[],
             [AS_VAR_SET(ac_Lib,[`$LDD conftest$ac_exeext | grep "ac_lib_pattern\\.$LIBEXT" | sed -e "s/^.*\(ac_lib_pattern\.$LIBEXT[[^	 ]]*\).*$/\1/"';2,$d'`])]) ;;
  esac],
  [AS_VAR_SET(ac_Lib,[])])
  LIBS=$ac_check_soname_save_LIBS])dnl
AS_VAR_IF([ac_Lib],[],
      [AC_MSG_RESULT([not found])
       $4],
      [AC_MSG_RESULT(AS_VAR_GET(ac_Lib))
       AC_DEFINE_UNQUOTED(AS_TR_CPP(SONAME_LIB$1),["]AS_VAR_GET(ac_Lib)["],
                          [Define to the soname of the lib$1 library.])
       $3])dnl
m4_popdef([ac_lib_pattern])dnl
AS_VAR_POPDEF([ac_Lib])])

dnl **** Get flags from pkg-config or alternate xxx-config program ****
dnl
dnl Usage: WINE_PACKAGE_FLAGS(var,pkg-name,[default-lib,[cflags-alternate,libs-alternate,[checks]]])
dnl
AC_DEFUN([WINE_PACKAGE_FLAGS],
[AC_REQUIRE([WINE_PATH_PKG_CONFIG])dnl
AS_VAR_PUSHDEF([ac_cflags],[[$1]_CFLAGS])dnl
AS_VAR_PUSHDEF([ac_libs],[[$1]_LIBS])dnl
rm -f conftest.err
AC_ARG_VAR(ac_cflags, [C compiler flags for $2, overriding pkg-config])dnl
AS_VAR_IF([ac_cflags],[],
      [AS_VAR_SET_IF([PKG_CONFIG],
      [ac_cflags=`$PKG_CONFIG --cflags [$2] 2>conftest.err`])])
m4_ifval([$4],[test "$cross_compiling" = yes || ac_cflags=[$]{ac_cflags:-[$4]}])
AC_ARG_VAR(ac_libs, [Linker flags for $2, overriding pkg-config])dnl
AS_VAR_IF([ac_libs],[],
      [AS_VAR_SET_IF([PKG_CONFIG],
      [ac_libs=`$PKG_CONFIG --libs [$2] 2>/dev/null`])])
m4_ifval([$5],[test "$cross_compiling" = yes || ac_libs=[$]{ac_libs:-[$5]}])
m4_ifval([$3],[ac_libs=[$]{ac_libs:-"$3"}])
AS_ECHO(["$as_me:${as_lineno-$LINENO}: $2 cflags: $ac_cflags"]) >&AS_MESSAGE_LOG_FD
AS_ECHO(["$as_me:${as_lineno-$LINENO}: $2 libs: $ac_libs"]) >&AS_MESSAGE_LOG_FD
if test -s conftest.err; then
     AS_ECHO_N(["$as_me:${as_lineno-$LINENO}: $2 errors: "]) >&AS_MESSAGE_LOG_FD
     cat conftest.err >&AS_MESSAGE_LOG_FD
fi
rm -f conftest.err
ac_save_CPPFLAGS=$CPPFLAGS
CPPFLAGS="$CPPFLAGS $ac_cflags"
$6
CPPFLAGS=$ac_save_CPPFLAGS
AS_VAR_POPDEF([ac_libs])dnl
AS_VAR_POPDEF([ac_cflags])])dnl

dnl **** Get flags from MinGW pkg-config or alternate xxx-config program ****
dnl
dnl Usage: WINE_MINGW_PACKAGE_FLAGS(var,pkg-name,[default-lib,[checks]])
dnl
AC_DEFUN([WINE_MINGW_PACKAGE_FLAGS],
[AC_REQUIRE([WINE_PATH_MINGW_PKG_CONFIG])dnl
AS_VAR_PUSHDEF([ac_cflags],[[$1]_PE_CFLAGS])dnl
AS_VAR_PUSHDEF([ac_libs],[[$1]_PE_LIBS])dnl
AS_VAR_IF([ac_cflags],[],
      [AS_VAR_SET_IF([MINGW_PKG_CONFIG],
      [ac_cflags=`$MINGW_PKG_CONFIG --cflags [$2] 2>/dev/null`])])
AS_VAR_IF([ac_libs],[],
      [AS_VAR_SET_IF([MINGW_PKG_CONFIG],
      [ac_libs=`$MINGW_PKG_CONFIG --libs [$2] 2>/dev/null`])])
m4_ifval([$3],[ac_libs=[$]{ac_libs:-"$3"}])
ac_save_CPPFLAGS=$CPPFLAGS
CPPFLAGS="$CPPFLAGS $ac_cflags"
$4
CPPFLAGS=$ac_save_CPPFLAGS
AS_VAR_POPDEF([ac_libs])dnl
AS_VAR_POPDEF([ac_cflags])])dnl

dnl **** Get flags for an external lib program ****
dnl
dnl Usage: WINE_EXTLIB_FLAGS(var,pkg-name,default-libs,default-cflags)
dnl
AC_DEFUN([WINE_EXTLIB_FLAGS],
[AS_VAR_PUSHDEF([ac_cflags],[[$1]_PE_CFLAGS])dnl
AS_VAR_PUSHDEF([ac_libs],[[$1]_PE_LIBS])dnl
AS_VAR_PUSHDEF([ac_enable],[enable_[$2]])dnl
AC_ARG_VAR(ac_cflags, [C compiler flags for the PE $2, overriding the bundled version])dnl
AC_ARG_VAR(ac_libs, [Linker flags for the PE $2, overriding the bundled version])dnl
AS_VAR_IF([ac_libs],[],
  [ac_libs=$3
   AS_VAR_IF([ac_cflags],[],[ac_cflags=$4],[ac_enable=no])],
  [ac_enable=no])
AS_ECHO(["$as_me:${as_lineno-$LINENO}: $2 cflags: $ac_cflags"]) >&AS_MESSAGE_LOG_FD
AS_ECHO(["$as_me:${as_lineno-$LINENO}: $2 libs: $ac_libs"]) >&AS_MESSAGE_LOG_FD
AS_VAR_POPDEF([ac_enable])dnl
AS_VAR_POPDEF([ac_libs])dnl
AS_VAR_POPDEF([ac_cflags])])dnl

dnl **** Link C code with an assembly file ****
dnl
dnl Usage: WINE_TRY_ASM_LINK(asm-code,includes,function,[action-if-found,[action-if-not-found]])
dnl
AC_DEFUN([WINE_TRY_ASM_LINK],
[AC_LINK_IFELSE([AC_LANG_PROGRAM([[$2]],[[asm($1); $3]])],[$4],[$5])])

dnl **** Check if we can link an empty program with special CFLAGS ****
dnl
dnl Usage: WINE_TRY_CFLAGS(flags,[action-if-yes,[action-if-no]])
dnl
dnl The default action-if-yes is to append the flags to EXTRACFLAGS.
dnl
AC_DEFUN([WINE_TRY_CFLAGS],
[AS_VAR_PUSHDEF([ac_var], ac_cv_cflags_[[$1]])dnl
AC_CACHE_CHECK([whether the compiler supports $1], ac_var,
[ac_wine_try_cflags_saved=$CFLAGS
CFLAGS="$CFLAGS $1"
AC_LINK_IFELSE([AC_LANG_SOURCE([[int main(int argc, char **argv) { return 0; }]])],
               [AS_VAR_SET(ac_var,yes)], [AS_VAR_SET(ac_var,no)])
CFLAGS=$ac_wine_try_cflags_saved])
AS_VAR_IF([ac_var],[yes],[m4_default([$2], [EXTRACFLAGS="$EXTRACFLAGS $1"])], [$3])dnl
AS_VAR_POPDEF([ac_var])])

dnl **** Check if we can link an empty program with special CFLAGS ****
dnl
dnl Usage: WINE_TRY_PE_CFLAGS(flags,[action-if-yes,[action-if-no]])
dnl
dnl The default action-if-yes is to append the flags to the arch-specific EXTRACFLAGS.
dnl
AC_DEFUN([WINE_TRY_PE_CFLAGS],
[{ AS_VAR_PUSHDEF([ac_var], ac_cv_${wine_arch}_cflags_[[$1]])dnl
AC_CACHE_CHECK([whether $CC supports $1], ac_var,
[ac_wine_try_cflags_saved=$CFLAGS
ac_wine_try_cflags_saved_exeext=$ac_exeext
CFLAGS="$CFLAGS -nostdlib -nodefaultlibs $1"
ac_exeext=".exe"
AC_LINK_IFELSE([AC_LANG_SOURCE([[void *__os_arm64x_dispatch_ret = 0;
const unsigned int _load_config_used[0x50] = { sizeof(_load_config_used) };
#if defined(__clang_major__) && defined(MIN_CLANG_VERSION) && __clang_major__ < MIN_CLANG_VERSION
#error Too old clang version
#endif
int __cdecl mainCRTStartup(void) { return 0; }]])],
               [AS_VAR_SET(ac_var,yes)], [AS_VAR_SET(ac_var,no)])
CFLAGS=$ac_wine_try_cflags_saved
ac_exeext=$ac_wine_try_cflags_saved_exeext])
AS_VAR_IF([ac_var],[yes],[m4_default([$2], [AS_VAR_APPEND([${wine_arch}_EXTRACFLAGS],[" $1"])])], [$3])dnl
AS_VAR_POPDEF([ac_var]) }])

dnl **** Check whether the given MinGW header is available ****
dnl
dnl Usage: WINE_CHECK_MINGW_HEADER(header,[action-if-found],[action-if-not-found],[other-includes])
dnl
AC_DEFUN([WINE_CHECK_MINGW_HEADER],
[AS_VAR_PUSHDEF([ac_var],[ac_cv_mingw_header_$1])dnl
AC_CACHE_CHECK([for MinGW $1], ac_var,
[ac_wine_check_headers_saved_cc=$CC
ac_wine_check_headers_saved_exeext=$ac_exeext
AS_VAR_COPY([CC],[${wine_arch}_CC])
ac_exeext=".exe"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[$4
#include <$1>]])],[AS_VAR_SET([ac_var],[yes])],[AS_VAR_SET([ac_var],[no])])
CC=$ac_wine_check_headers_saved_cc
ac_exeext=$ac_wine_check_headers_saved_exeext])
AS_VAR_IF([ac_var],[yes],[$2],[$3])dnl
AS_VAR_POPDEF([ac_var])])

dnl **** Check whether the given MinGW library is available ****
dnl
dnl Usage: WINE_CHECK_MINGW_LIB(library,function,[action-if-found],[action-if-not-found],[other-libraries])
dnl
AC_DEFUN([WINE_CHECK_MINGW_LIB],
[AS_VAR_PUSHDEF([ac_var],[ac_cv_mingw_lib_$1])dnl
AC_CACHE_CHECK([for $2 in MinGW -l$1], ac_var,
[ac_wine_check_headers_saved_cc=$CC
ac_wine_check_headers_saved_exeext=$ac_exeext
ac_wine_check_headers_saved_libs=$LIBS
AS_VAR_COPY([CC],[${wine_arch}_CC])
ac_exeext=".exe"
LIBS="-l$1 $5 $LIBS"
AC_LINK_IFELSE([AC_LANG_CALL([], [$2])],[AS_VAR_SET([ac_var],[yes])],[AS_VAR_SET([ac_var],[no])])
CC=$ac_wine_check_headers_saved_cc
ac_exeext=$ac_wine_check_headers_saved_exeext
LIBS=$ac_wine_check_headers_saved_libs])
AS_VAR_IF([ac_var],[yes],[$3],[$4])dnl
AS_VAR_POPDEF([ac_var])])

dnl **** Check whether we need to define a symbol on the compiler command line ****
dnl
dnl Usage: WINE_CHECK_DEFINE(name),[action-if-yes,[action-if-no]])
dnl
AC_DEFUN([WINE_CHECK_DEFINE],
[AS_VAR_PUSHDEF([ac_var],[ac_cv_cpp_def_$1])dnl
AC_CACHE_CHECK([whether we need to define $1],ac_var,
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#ifdef $1
#error no
#endif])],[AS_VAR_SET([ac_var],[yes])],[AS_VAR_SET([ac_var],[no])]))
AS_VAR_IF([ac_var],[yes],[EXTRACFLAGS="$EXTRACFLAGS -D$1"])dnl
AS_VAR_POPDEF([ac_var])])

dnl **** Check for functions with some extra libraries ****
dnl
dnl Usage: WINE_CHECK_LIB_FUNCS(funcs,libs,[action-if-found,[action-if-not-found]])
dnl
AC_DEFUN([WINE_CHECK_LIB_FUNCS],
[ac_wine_check_funcs_save_LIBS="$LIBS"
LIBS="$LIBS $2"
AC_CHECK_FUNCS([$1],[$3],[$4])
LIBS="$ac_wine_check_funcs_save_LIBS"])

dnl **** Define helper functions for creating config.status files ****
dnl
dnl Usage: AC_REQUIRE([WINE_CONFIG_HELPERS])
dnl
AC_DEFUN([WINE_CONFIG_HELPERS],
[AS_VAR_SET([wine_rules],["all:
	@echo 'Wine build complete.'"])
AC_SUBST(SUBDIRS,"")
AC_SUBST(DISABLED_SUBDIRS,"")
AC_SUBST(CONFIGURE_TARGETS,"")

wine_fn_config_makefile ()
{
    AS_VAR_APPEND([SUBDIRS],[" \\$as_nl	$[1]"])
    AS_VAR_COPY([enable],[$[2]])
    case "$enable" in
      no) AS_VAR_APPEND([DISABLED_SUBDIRS],[" $[1]"]) ;;
      yes) ;;
      *aarch64*|*arm*|*i386*|*x86_64*)
        if test -n "$PE_ARCHS" -a "$PE_ARCHS" != none
        then
            for i in $PE_ARCHS
            do
                test $(expr ",$enable," : ".*,$i,") -gt 0 || AS_VAR_APPEND([${i}_DISABLED_SUBDIRS],[" $[1]"])
            done
        else
            test $(expr ",$enable," : ".*,$HOST_ARCH,") -gt 0 || AS_VAR_APPEND([DISABLED_SUBDIRS],[" $[1]"])
        fi ;;
      "")
        case "$[1], $PE_ARCHS " in
          programs/*,*\ arm64ec\ *) AS_VAR_APPEND([arm64ec_DISABLED_SUBDIRS],[" $[1]"]) ;;
        esac ;;
    esac
}])

dnl **** Define helper function to append a rule to a makefile command list ****
dnl
dnl Usage: WINE_APPEND_RULE(rule)
dnl
AC_DEFUN([WINE_APPEND_RULE],[AC_REQUIRE([WINE_CONFIG_HELPERS])AS_VAR_APPEND([wine_rules],["
$1"])])

dnl **** Create a makefile from config.status ****
dnl
dnl Usage: WINE_CONFIG_MAKEFILE(file)
dnl
AC_DEFUN([WINE_CONFIG_MAKEFILE],[AC_REQUIRE([WINE_CONFIG_HELPERS])dnl
AS_VAR_PUSHDEF([ac_enable],[enable_]m4_bpatsubst(m4_bpatsubst([$1],[.*/\([^/]*\)$],[\1]),[.*\.\(.*16\|vxd\)$],[win16]))dnl
m4_append_uniq([_AC_USER_OPTS],ac_enable,[
])dnl
m4_if(m4_bregexp([$1],[^tools]),[-1],[],[test "x$enable_tools" = xno || ])wine_fn_config_makefile [$1] ac_enable[]dnl
AS_VAR_POPDEF([ac_enable])])

dnl **** Append a file to the .gitignore list ****
dnl
dnl Usage: WINE_IGNORE_FILE(file,enable)
dnl
AC_DEFUN([WINE_IGNORE_FILE],[AC_REQUIRE([WINE_CONFIG_HELPERS])dnl
m4_ifval([$2],[test "x$[$2]" = xno || ])AS_VAR_APPEND([CONFIGURE_TARGETS],[" $1"])])

dnl **** Add a message to the list displayed at the end ****
dnl
dnl Usage: WINE_NOTICE(notice)
dnl Usage: WINE_WARNING(warning)
dnl Usage: WINE_NOTICE_WITH(with_flag, test, notice, enable)
dnl Usage: WINE_WARNING_WITH(with_flag, test, warning, enable)
dnl Usage: WINE_ERROR_WITH(with_flag, test, error, enable)
dnl Usage: WINE_PRINT_MESSAGES
dnl
AC_DEFUN([WINE_NOTICE],[AS_VAR_APPEND([wine_notices],["|$1"])])
AC_DEFUN([WINE_WARNING],[AS_VAR_APPEND([wine_warnings],["|$1"])])

AC_DEFUN([WINE_NOTICE_WITH],[AS_IF([$2],[case "x$with_$1" in
  x)   WINE_NOTICE([$3]) ;;
  xno) ;;
  *)   AC_MSG_ERROR([$3
This is an error since --with-$1 was requested.]) ;;
esac
m4_ifval([$4],[$4=${$4:-no}])])])

AC_DEFUN([WINE_WARNING_WITH],[AS_IF([$2],[case "x$with_$1" in
  x)   WINE_WARNING([$3]) ;;
  xno) ;;
  *)   AC_MSG_ERROR([$3
This is an error since --with-$1 was requested.]) ;;
esac
m4_ifval([$4],[$4=${$4:-no}])])])

AC_DEFUN([WINE_ERROR_WITH],[AS_IF([$2],[case "x$with_$1" in
  xno) ;;
  *)   AC_MSG_ERROR([$3
Use the --without-$1 option if you really want this.]) ;;
esac
m4_ifval([$4],[$4=${$4:-no}])])])

AC_DEFUN([WINE_PRINT_MESSAGES],[ac_save_IFS="$IFS"
if test "x$wine_notices" != x; then
    echo >&AS_MESSAGE_FD
    IFS="|"
    for msg in $wine_notices; do
        IFS="$ac_save_IFS"
        AS_VAR_IF([msg],[],,[AC_MSG_NOTICE([$msg])])
    done
fi
IFS="|"
for msg in $wine_warnings; do
    IFS="$ac_save_IFS"
    AS_VAR_IF([msg],[],,[echo >&2
        AC_MSG_WARN([$msg])])
done
IFS="$ac_save_IFS"])

dnl Local Variables:
dnl compile-command: "autoreconf --warnings=all"
dnl End:
