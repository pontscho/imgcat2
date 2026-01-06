/**
 * @file main.c
 * @brief Main entry point for integration tests
 *
 * Integration tests validate end-to-end functionality of imgcat2.
 */

#define CTEST_MAIN

#include "../ctest.h"

int main(int argc, const char *argv[])
{
	return ctest_main(argc, argv);
}
