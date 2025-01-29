// File: proj1.h

#ifndef PROJ1_H
#define PROJ1_H

#include <stdbool.h>
#include <stddef.h>  // optional if you use size_t
#include <stdlib.h>  // optional if you use malloc/free/NULL
#include <string.h>  // optional if you use strcmp, etc.

/**
 * A struct to hold a user-defined macro:
 *   - name: The alphanumeric name of the macro (e.g., "FOO")
 *   - value: The replacement string (may contain '#' for the argument)
 */
typedef struct Macro {
    char *name;
    char *value;
    struct Macro *next; // singly-linked list
} Macro;

/**
 * A table of macros, represented as a singly-linked list head pointer
 */
typedef struct {
    Macro *head;
} MacroTable;

/**
 * Initializes a macro table.
 */
void init_macro_table(MacroTable *table);

/**
 * Defines a macro with the given name and value.
 * If the macro already exists, it's an error (must exit).
 */
void define_macro(MacroTable *table, const char *name, const char *value);

/**
 * Removes (undefines) a macro with the given name.
 * If the macro doesn't exist, it's an error (must exit).
 */
void undef_macro(MacroTable *table, const char *name);

/**
 * Looks up a macro by name.
 * Returns the replacement string if found, or NULL if not found.
 */
char *lookup_macro(MacroTable *table, const char *name);

/**
 * Frees all macros in the table.
 */
void free_macro_table(MacroTable *table);

/**
 * parse_and_expand:
 *   1. Reads all files specified in argv (or stdin if none).
 *   2. Removes comments.
 *   3. Parses macros.
 *   4. Performs expansions.
 *   5. Outputs the final result to stdout.
 *
 * Exits on any error.
 */
void parse_and_expand(MacroTable *table, int argc, char *argv[]);

#endif // PROJ1_H
