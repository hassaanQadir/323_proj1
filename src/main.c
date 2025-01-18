// File: main.c

#include "parser.h"
#include "macros.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    // Initialize a macro table
    MacroTable table;
    init_macro_table(&table);

    // Parse all input, expand macros, and print to stdout
    parse_and_expand(&table, argc, argv);

    // Done. If you want to be thorough, free macros here:
    free_macro_table(&table);

    return 0; // success
}
