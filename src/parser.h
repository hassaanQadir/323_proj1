// File: parser.h

#ifndef PARSER_H
#define PARSER_H

#include "macros.h"

void parse_and_expand(MacroTable *table, int argc, char *argv[]);
/*
 * This function will:
 * 1. Read all files specified in argv (or stdin if none)
 * 2. Remove comments
 * 3. Parse macros
 * 4. Perform expansions
 * 5. Output the final result to stdout
 *
 * Exits on any error.
 */

#endif
