# Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
# Copyright (C) 2011 Christophe Fergeau <cfergeau@redhat.com>
AC_PREREQ(2.63)

AC_INIT([vdagent-win],
        m4_esyscmd([build-aux/git-version-gen .tarball-version]))

AC_CONFIG_SRCDIR([vdagent])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CANONICAL_HOST

AM_INIT_AUTOMAKE([1.11 foreign subdir-objects no-dist-gzip dist-zip dist-xz tar-ustar])

AM_SILENT_RULES([yes])

AC_ARG_WITH([buildid],
AS_HELP_STRING([--with-buildid=id], [Set additional build version details]))
AC_DEFINE_UNQUOTED([BUILDID], "$with_buildid", [Build version details])
if test "x$with_buildid" != x; then
  AC_SUBST([BUILDID], "-$with_buildid")
fi

# git-version-gen emits 0.10.1 or 0.10.0.83-abcde (commits since the tag).
# MSI ProductVersion is major.minor.build. Using only the third field left
# untagged builds at 0.10.0, so Windows reported "Product already installed".
major=`echo "$PACKAGE_VERSION" | cut -d. -f1`
minor=`echo "$PACKAGE_VERSION" | cut -d. -f2`
micro=`echo "$PACKAGE_VERSION" | cut -d. -f3 | cut -d- -f1`
distance=`echo "$PACKAGE_VERSION" | cut -d. -f4 | cut -d- -f1`
case $micro in ''|*[!0-9]*) micro=0 ;; esac
case $distance in ''|*[!0-9]*) distance=0 ;; esac
buildid=`echo $with_buildid | cut -d. -f1`
if test "x$buildid" = x; then
  buildid=0
fi
case $buildid in *[!0-9]*) buildid=0 ;; esac
build=`expr $micro \* 256 + $distance + $buildid`
WINDOWS_PRODUCTVERSION="$major.$minor.$build"
AC_SUBST([WINDOWS_PRODUCTVERSION])
RC_PRODUCTVERSION="$major, $minor, $micro, $distance"
AC_DEFINE_UNQUOTED([RC_PRODUCTVERSION_STR], "$RC_PRODUCTVERSION", [Resource product version])
AC_DEFINE_UNQUOTED([RC_PRODUCTVERSION], [$RC_PRODUCTVERSION], [Resource product version])
BUILD_YEAR=`date +%Y`
AC_DEFINE_UNQUOTED([BUILD_YEAR], "$BUILD_YEAR", [Build year])
RPM_VERSION=`echo $PACKAGE_VERSION | sed 's,-,_,g'`
AC_SUBST([RPM_VERSION])

# Check for programs
AC_PROG_CC
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX_11
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_CHECK_TOOL(WINDRES, [windres])

case "$host" in
amd64*|x86_64*)
  WIXL_ARCH="x64"
;;
*)
  WIXL_ARCH="x86"
;;
esac
AC_SUBST(WIXL_ARCH)

AC_ARG_ENABLE([debug],
    AS_HELP_STRING([--enable-debug], [Enable debugging]))

AM_CONDITIONAL([ENABLE_DEBUG], [test "x$enable_debug" = "xyes"])

dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------

if test "$GCC" = "yes"; then
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wall"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wcast-align -Wno-uninitialized"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-declarations"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wpointer-arith"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wcast-align"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wwrite-strings"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Winit-self"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wreturn-type"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wformat-nonliteral"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wformat-security"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-include-dirs"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-format-attribute"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wclobbered"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wempty-body"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wignored-qualifiers"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wsign-compare"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wtype-limits"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wuninitialized"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Waggregate-return"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wdeclaration-after-statement"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wshadow"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wno-strict-aliasing"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Winline"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-parameter-type"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Woverride-init"
else
	WARNINGFLAGS_C=""
fi
AC_SUBST(WARNINGFLAGS_C)

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------

ACX_SET_VAR(CFLAGS, "$CFLAGS -Wall",
  ACX_SET_VAR(LDFLAGS, "$LDFLAGS -static",
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdio.h>]], [[
      printf("hello");
      return 0
    ]])], [], AC_MSG_ERROR([some base static library (possibly pthread) not found]))
  )
)

dnl ---------------------------------------------------------------------------
dnl - Makefiles, etc.
dnl ---------------------------------------------------------------------------
AC_CONFIG_FILES([
Makefile
mingw-spice-vdagent.spec
spice-vdagent.wxs
])
AC_OUTPUT

dnl ==========================================================================
echo "
                  vdagent-win $VERSION
                  ================

        prefix:                    ${prefix}
        datadir:                   ${datadir}
        compiler:                  ${CXX}
        cxxflags:                  ${CXXFLAGS}
        cppflags:                  ${CPPFLAGS}
"
