/**
 * @file main.c
 * @brief Test runner main entry point for unit tests
 *
 * Defines CTEST_MAIN to include ctest.h implementation.
 */

#define CTEST_MAIN
#include "../ctest.h"

int main(int argc, const char *argv[])
{
	return ctest_main(argc, argv);
}
