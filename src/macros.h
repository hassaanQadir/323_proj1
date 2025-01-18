// File: macros.h

#ifndef MACROS_H
#define MACROS_H

#include <stdbool.h>

// A struct to hold a user-defined macro:
//   name: The alphanumeric name of the macro (e.g., "FOO")
//   value: The replacement string, may contain '#' for the argument
typedef struct Macro {
    char *name;
    char *value;
    struct Macro *next; // singly-linked list
} Macro;

// Maintain a linked list of macros
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

#endif
