#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([zmq-test.cpp])
AM_INIT_AUTOMAKE(zmqtest, 0.1)
AM_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([config])

# Checks for programs.
AC_LANG([C++])
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX_11
AM_MISSING_PROG(HELP2MAN, help2man, $missing_dir)

# Checks for libraries.
AC_CHECK_LIB([boost_program_options], [_ZN5boost15program_options13variables_mapC1Ev], [], [AC_MSG_ERROR([Require boost_program_options])])
AC_CHECK_LIB([zmq], [zmq_init], [], [AC_MSG_ERROR([Require zmq])])

# Checks for header files.
AC_CHECK_HEADERS([zmq.h])
AC_CHECK_HEADERS([boost/program_options.hpp])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
