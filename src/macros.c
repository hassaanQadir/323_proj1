// File: macros.c

#include "macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static Macro *create_macro(const char *name, const char *value) {
    Macro *m = malloc(sizeof(Macro));
    if (!m) {
        fprintf(stderr, "Error: malloc failed in create_macro\n");
        exit(EXIT_FAILURE);
    }
    m->name = strdup(name);
    m->value = strdup(value);
    m->next = NULL;
    return m;
}

void init_macro_table(MacroTable *table) {
    table->head = NULL;
}

void define_macro(MacroTable *table, const char *name, const char *value) {
    // Check if name is already defined
    for (Macro *cur = table->head; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            fprintf(stderr, "Error: Macro '%s' already defined\n", name);
            exit(EXIT_FAILURE);
        }
    }
    // Create new macro
    Macro *new_macro = create_macro(name, value);
    // Insert at head
    new_macro->next = table->head;
    table->head = new_macro;
}

void undef_macro(MacroTable *table, const char *name) {
    Macro *prev = NULL;
    for (Macro *cur = table->head; cur; prev = cur, cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            // Remove it
            if (prev) {
                prev->next = cur->next;
            } else {
                table->head = cur->next;
            }
            free(cur->name);
            free(cur->value);
            free(cur);
            return;
        }
    }
    // If we get here, macro wasn't found
    fprintf(stderr, "Error: Cannot undefine '%s' - not defined\n", name);
    exit(EXIT_FAILURE);
}

char *lookup_macro(MacroTable *table, const char *name) {
    for (Macro *cur = table->head; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            return cur->value; // returning pointer is okay
        }
    }
    return NULL; // not found
}

void free_macro_table(MacroTable *table) {
    Macro *cur = table->head;
    while (cur) {
        Macro *temp = cur;
        cur = cur->next;
        free(temp->name);
        free(temp->value);
        free(temp);
    }
    table->head = NULL;
}
