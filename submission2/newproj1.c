#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// A singly-linked list node representing a macro with a name and a value
typedef struct Macro{
    char *name;
    char *value;
    struct Macro *next;
} Macro;

// The macro table tracks the head of the linked list
typedef struct {
    Macro *head;
} MacroTable;

// Initialize the macro table (sets head to NULL)
static void init_macro_table(MacroTable *table) {
    table->head = NULL;
}

// Helper to allocate and initialize a new Macro struct
static Macro *create_macro(const char *name, const char *value) {
    Macro *m = malloc(sizeof(Macro));
    if (!m) {
        fprintf(stderr, "Error: malloc failed in create_macro\n");
        exit(EXIT_FAILURE);
    }
    m->name = strdup(name);
    if (!m->name) {
        fprintf(stderr, "Error: strdup failed in create_macro (name)\n");
        exit(EXIT_FAILURE);
    }
    m->value = strdup(value);
    if (!m->value) {
        fprintf(stderr, "Error: strdup failed in create_macro (value)\n");
        exit(EXIT_FAILURE);
    }
    m->next = NULL;
    return m;
}

// Defines a new macro in the table (inserts at the head of the list)
// Exits with an error if the macro already exists
static void define_macro(MacroTable *table, const char *name, const char *value) {
    for (Macro *cur = table->head; cur; cur=cur->next) {
        if (strcmp(cur->name, name) == 0) {
            fprintf(stderr, "Error: Macro '%s' already defined\n", name);
            exit(EXIT_FAILURE);
        }
    }
    Macro *new_macro = create_macro(name, value);
    new_macro->next = table->head;
    table->head = new_macro;
}

// Undefine a macro from the table by name
// Exits with an error if the macro is not found
static void undef_macro(MacroTable *table, const char *name) {
    Macro *prev = NULL;
    Macro *cur = table->head;

    while (cur) {
        if (strcmp(cur->name, name) == 0) {
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
        prev = cur;
        cur = cur->next;
    }
    fprintf(stderr, "Error: Cannot undefine '%s' - not defined\n", name);
    exit(EXIT_FAILURE);
}

// Look up a macro by name and return its value, or NULL if not found
static char *lookup_macro(MacroTable *table, const char *name) {
    for (Macro *cur = table->head; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            return cur->value;
        }
    }
    return NULL;
}

// Free all memory used by the macro table
static void free_macro_table(MacroTable *table) {
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

// Define a function pointer to handle output of characters to
// either print to stdout or accumulate in a buffer
typedef void (*output_char_fn)(char c, void *userdata);

// A simple struct to expand text into a dynamic buffer
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} OutputBuffer;

// Forward declarations
static void expand_text_impl(MacroTable *table, const char *input,
                             output_char_fn out_char, void *userdata);
static char *expand_text_into_string(MacroTable *table, const char *input);
static char *combine_strings(const char *s1, const char *s2);
static char *read_included_file(const char *path);
static char *readArg(const char **pp);

// Output function that writes a character to stdout
static void out_char_stdout(char c, void *userdata) {
    (void)userdata;
    putchar(c);
}

// Output function that appends a character to an OutputBuffer
static void out_char_buffer(char c, void *userdata) {
    OutputBuffer *ob = (OutputBuffer *)userdata;
    if (ob->len + 1 >= ob->cap) {
        ob->cap *= 2;
        ob->buf = realloc(ob->buf, ob->cap);
        if (!ob->buf) {
            fprintf(stderr, "Error: realloc failed in out_char_buffer\n");
            exit(EXIT_FAILURE);
        }
    }
    ob->buf[ob->len++] = c;
    ob->buf[ob->len] = '\0';
}

// Read all input from stdin or files from the cmd lne,
// removing any lines that start with '%' (unless prefixed with a backslash).
// Also removes leading spaces/tabs after those lines
static char *read_all_input_and_remove_comments(int argc, char *argv[]) {
    size_t cap = 8192;
    size_t len = 0;
    char *buffer = malloc(cap);
    if (!buffer) {
        fprintf(stderr, "Error: malloc failed in read_all_input_and_remove_comments\n");
        exit(EXIT_FAILURE);
    }

#define APPEND_CHAR(c) \
    do { \
        if (len + 1 >= cap) { \
            cap *= 2; \
            buffer = realloc(buffer, cap); \
            if (!buffer) { \
                fprintf(stderr, "Error: realloc failed\n"); \
                exit(EXIT_FAILURE); \
            } \
        } \
        buffer[len++] = (c); \
    } while (0)

    int c, prevChar = 0;

    if (argc < 2) {
        /* Reading from stdin */
        while ((c = fgetc(stdin)) != EOF) {
            if (c == '%' && prevChar != '\\') {
                /* Skip until newline */
                while ((c = fgetc(stdin)) != EOF && c != '\n')
                    ;
                /* Now skip leading blanks/tabs on the next line */
                if (c == '\n') {
                    int next_c;
                    while ((next_c = fgetc(stdin)) != EOF && 
                           (next_c == ' ' || next_c == '\t'))
                    {
                        /* Just skip these */
                    }
                    if (next_c != EOF) {
                        ungetc(next_c, stdin);
                    }
                }
                prevChar = 0;
                continue;
            }
            APPEND_CHAR(c);
            prevChar = c;
        }
    } else {
        /* Reading from files passed as arguments */
        for (int i = 1; i < argc; i++) {
            FILE *fp = fopen(argv[i], "r");
            if (!fp) {
                fprintf(stderr, "Error: Cannot open file '%s'\n", argv[i]);
                exit(EXIT_FAILURE);
            }
            prevChar = 0;
            while ((c = fgetc(fp)) != EOF) {
                if (c == '%' && prevChar != '\\') {
                    while ((c = fgetc(fp)) != EOF && c != '\n')
                        ;
                    if (c == '\n') {
                        int next_c;
                        while ((next_c = fgetc(fp)) != EOF && 
                               (next_c == ' ' || next_c == '\t'))
                        {
                            /* Skip leading spaces/tabs */
                        }
                        if (next_c != EOF) {
                            ungetc(next_c, fp);
                        }
                    }
                    prevChar = 0;
                    continue;
                }
                APPEND_CHAR(c);
                prevChar = c;
            }
            fclose(fp);
        }
    }

    APPEND_CHAR('\0');
#undef APPEND_CHAR
    return buffer;
}
        
// Read an included file's entire contents, also removing lines
// that start with '%', plus leading spaces on new lines.
static char *read_included_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", path);
        exit(EXIT_FAILURE);
    }

    size_t cap = 8192;
    size_t len = 0;
    char *buffer = malloc(cap);
    if (!buffer) {
        fprintf(stderr, "Error: malloc failed in read_included_file\n");
        exit(EXIT_FAILURE);
    }

#define APPEND_CHAR2(c) \
    do { \
        if (len + 1 >= cap) { \
            cap *= 2; \
            buffer = realloc(buffer, cap); \
            if (!buffer) { \
                fprintf(stderr, "Error: realloc failed\n"); \
                exit(EXIT_FAILURE); \
            } \
        } \
        buffer[len++] = (c); \
    } while (0)

    int c, prev = 0;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '%' && prev != '\\') {
            while ((c = fgetc(fp)) != EOF && c != '\n')
                ;
            if (c == '\n') {
                int next_c;
                while ((next_c = fgetc(fp)) != EOF && 
                       (next_c == ' ' || next_c == '\t'))
                {
                    /* skip */
                }
                if (next_c != EOF) {
                    ungetc(next_c, fp);
                }
            }
            prev = 0;
            continue;
        }
        APPEND_CHAR2(c);
        prev = c;
    }
    fclose(fp);
    APPEND_CHAR2('\0');
#undef APPEND_CHAR2
    return buffer;
}

// Expand input into a newly allocated string and return the string
static char *expand_text_into_string(MacroTable *table, const char *input) {
    OutputBuffer ob;
    ob.len = 0;
    ob.cap = 256;
    ob.buf = malloc(ob.cap);
    if (!ob.buf) {
        fprintf(stderr, "Error: malloc failed in expand_text_into_string\n");
        exit(EXIT_FAILURE);
    }
    ob.buf[0] = '\0';

    expand_text_impl(table, input, out_char_buffer, &ob);
    return ob.buf;
}

// Combine two strings into a newly allocated buffer
static char *combine_strings(const char *s1, const char *s2) {
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    char *res = malloc(len1 + len2 + 1);
    if (!res) {
        fprintf(stderr, "Error: malloc failed in combine_strings\n");
        exit(EXIT_FAILURE);
    }
    memcpy(res, s1, len1);
    memcpy(res + len1, s2, len2);
    res[len1 + len2] = '\0';
    return res;
}

// Parse a single macro argument of the form { ... }. The braces can be escaped
// with backslashes inside, so brace depth only changes when braces are not escaped.
static char *readArg(const char **pp) {
    if (**pp != '{') {
        return NULL;
    }
    (*pp)++;

    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        fprintf(stderr, "Error: malloc failed in readArg\n");
        exit(EXIT_FAILURE);
    }

    int braceDepth = 1;
    bool escaped = false;

    while (**pp && braceDepth > 0) {
        char c = **pp;
        (*pp)++;

        if (!escaped) {
            if (c == '\\') {
                escaped = true;
                buf[len++] = c;
            } else if (c == '{') {
                braceDepth++;
                buf[len++] = c;
            } else if (c == '}') {
                braceDepth--;
                if (braceDepth > 0) {
                    buf[len++] = c;
                }
            } else {
                buf[len++] = c;
            }
        } else {
            // The previous character was a backslash so take c literally
            buf[len++] = c;
            escaped = false;
        }

        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) {
                fprintf(stderr, "Error: realloc failed in readArg\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    if (braceDepth != 0) {
        free(buf);
        fprintf(stderr, "Error: unbalanced braces in argument\n");
        exit(EXIT_FAILURE);
    }

    buf[len] = '\0';
    return buf;
}

// Read a macro name starting immediately after a backslash
// Continues while the characters are alphanumeric
static char *readMacroName(const char **pp) {
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        fprintf(stderr, "Error: malloc failed in readMacroName\n");
        exit(EXIT_FAILURE);
    }

    while (isalnum((unsigned char)**pp)) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) {
                fprintf(stderr, "Error: realloc failed in readMacroName\n");
                exit(EXIT_FAILURE);
            }
        }
        buf[len++] = **pp;
        (*pp)++;
    }
    buf[len] = '\0';
    return buf;
}

// The core function parses and expands the input
// Checks for backslash commands, macros, builtins, etc
static void expand_text_impl(MacroTable *table, const char *input,
                             output_char_fn out_char, void *userdata) {
    const char *p = input;
    while (*p != '\0') {
        if (*p == '\\') {
            p++;
            if (*p == '\0') {
                /* If we hit the end right after a backslash, just output it. */
                out_char('\\', userdata);
                break;
            }

            /* If the next char is one of the "special five" (\, {, }, #, %), 
               output only that second char. */
            if (*p == '\\' || *p == '{' || *p == '}' || *p == '#' || *p == '%') {
                out_char(*p, userdata);
                p++;
                continue;
            } 
            /* If it's alphanumeric, we might have a macro or a builtin command. */
            else if (isalnum((unsigned char)*p)) {
                char *nameBuf = readMacroName(&p);

                /* Builtin macros: \def, \undef, \if, \ifdef, \include, \expandafter */
                if (strcmp(nameBuf, "def") == 0) {
                    free(nameBuf);
                    char *arg1 = readArg(&p); // macro name
                    char *arg2 = readArg(&p); // macro value
                    if (!arg1 || !*arg1) {
                        fprintf(stderr, "Error: invalid macro name in \\def\n");
                        exit(EXIT_FAILURE);
                    }
                    if (!arg2) {
                        fprintf(stderr, "Error: invalid value in \\def\n");
                        exit(EXIT_FAILURE);
                    }
                    /* Validate that macro name is alphanumeric only. */
                    for (char *c = arg1; *c; c++) {
                        if (!isalnum((unsigned char)*c)) {
                            fprintf(stderr, "Error: invalid macro name in \\def\n");
                            exit(EXIT_FAILURE);
                        }
                    }
                    define_macro(table, arg1, arg2);
                    free(arg1);
                    free(arg2);
                    continue;
                }
                else if (strcmp(nameBuf, "undef") == 0) {
                    free(nameBuf);
                    char *arg1 = readArg(&p);
                    if (!arg1) {
                        fprintf(stderr, "Error: invalid \\undef usage\n");
                        exit(EXIT_FAILURE);
                    }
                    undef_macro(table, arg1);
                    free(arg1);
                    continue;
                }
                else if (strcmp(nameBuf, "if") == 0) {
                    free(nameBuf);
                    char *condArg = readArg(&p);
                    char *thenArg = readArg(&p);
                    char *elseArg = readArg(&p);
                    if (!condArg || !thenArg || !elseArg) {
                        fprintf(stderr, "Error: \\if requires 3 arguments\n");
                        exit(EXIT_FAILURE);
                    }
                    /* If condArg is empty, expand elseArg; otherwise expand thenArg. */
                    const char *branch = (condArg[0] == '\0') ? elseArg : thenArg;
                    const char *remaining = p;

                    expand_text_impl(table, branch, out_char, userdata);
                    /* Then expand whatever remains after the \if. */
                    expand_text_impl(table, remaining, out_char, userdata);

                    free(condArg);
                    free(thenArg);
                    free(elseArg);
                    return; 
                }
                else if (strcmp(nameBuf, "ifdef") == 0) {
                    free(nameBuf);
                    char *nameArg = readArg(&p);
                    char *thenArg = readArg(&p);
                    char *elseArg = readArg(&p);
                    if (!nameArg || !thenArg || !elseArg) {
                        fprintf(stderr, "Error: \\ifdef requires 3 arguments\n");
                        exit(EXIT_FAILURE);
                    }

                    /* Validate the macro name. */
                    for (char *c = nameArg; *c; c++) {
                        if (!isalnum((unsigned char)*c)) {
                            fprintf(stderr, "Error: invalid macro name in \\ifdef\n");
                            exit(EXIT_FAILURE);
                        }
                    }
                    char *macroVal = lookup_macro(table, nameArg);
                    const char *branch = (macroVal != NULL) ? thenArg : elseArg;
                    const char *remaining = p;

                    expand_text_impl(table, branch, out_char, userdata);
                    expand_text_impl(table, remaining, out_char, userdata);

                    free(nameArg);
                    free(thenArg);
                    free(elseArg);
                    return;
                }
                else if (strcmp(nameBuf, "include") == 0) {
                    free(nameBuf);
                    char *pathArg = readArg(&p);
                    if (!pathArg) {
                        fprintf(stderr, "Error: \\include requires 1 argument\n");
                        exit(EXIT_FAILURE);
                    }

                    const char *remaining = p;
                    char *includedText = read_included_file(pathArg);
                    char *combined = combine_strings(includedText, remaining);

                    expand_text_impl(table, combined, out_char, userdata);

                    free(includedText);
                    free(pathArg);
                    free(combined);
                    return;
                }
                else if (strcmp(nameBuf, "expandafter") == 0) {
                    free(nameBuf);
                    char *beforeArg = readArg(&p);
                    char *afterArg  = readArg(&p);
                    if (!beforeArg || !afterArg) {
                        fprintf(stderr, "Error: \\expandafter requires 2 arguments\n");
                        exit(EXIT_FAILURE);
                    }

                    const char *remaining = p;
                    char *expandedAfter = expand_text_into_string(table, afterArg);
                    char *combined = combine_strings(beforeArg, expandedAfter);
                    char *all = combine_strings(combined, remaining);

                    expand_text_impl(table, all, out_char, userdata);

                    free(beforeArg);
                    free(afterArg);
                    free(expandedAfter);
                    free(combined);
                    free(all);
                    return;
                }
                else {
                    /* If it's not a builtin, it's a user-defined macro. */
                    char *macroVal = lookup_macro(table, nameBuf);
                    if (!macroVal) {
                        fprintf(stderr, "Error: Macro '%s' not defined\n", nameBuf);
                        exit(EXIT_FAILURE);
                    }
                    char *arg1 = readArg(&p);
                    if (!arg1) {
                        fprintf(stderr, "Error: Macro '%s' used without argument\n", nameBuf);
                        exit(EXIT_FAILURE);
                    }
                    free(nameBuf);

                    /* Expand the macro by substituting '#' with the argument. */
                    const char *m = macroVal;
                    size_t outCap = 256;
                    size_t outLen = 0;
                    char *outBuf = malloc(outCap);
                    if (!outBuf) exit(EXIT_FAILURE);

                    while (*m) {
                        if (*m == '\\') {
                            /* We keep certain backslash sequences intact. */
                            char next = *(m + 1);
                            if (next == '\0') {
                                outBuf[outLen++] = '\\';
                                m++;
                            }
                            else if (next == '\\' || next == '{' || next == '}' ||
                                     next == '#'  || next == '%') {
                                outBuf[outLen++] = '\\';
                                outBuf[outLen++] = next;
                                m += 2;
                            }
                            else if (isalnum((unsigned char)next)) {
                                outBuf[outLen++] = '\\';
                                outBuf[outLen++] = next;
                                m += 2;
                            }
                            else {
                                outBuf[outLen++] = '\\';
                                outBuf[outLen++] = next;
                                m += 2;
                            }
                        } else if (*m == '#') {
                            /* Substitute the argument. */
                            size_t argLen = strlen(arg1);
                            while (outLen + argLen + 1 >= outCap) {
                                outCap *= 2;
                                outBuf = realloc(outBuf, outCap);
                                if (!outBuf) exit(EXIT_FAILURE);
                            }
                            memcpy(outBuf + outLen, arg1, argLen);
                            outLen += argLen;
                            m++;
                        } else {
                            outBuf[outLen++] = *m++;
                        }
                        if (outLen + 1 >= outCap) {
                            outCap *= 2;
                            outBuf = realloc(outBuf, outCap);
                            if (!outBuf) exit(EXIT_FAILURE);
                        }
                    }
                    outBuf[outLen] = '\0';

                    const char *remaining = p;
                    expand_text_impl(table, outBuf, out_char, userdata);
                    expand_text_impl(table, remaining, out_char, userdata);

                    free(outBuf);
                    free(arg1);
                    return;
                }
            } else {
                /* If backslash is followed by a non-special, non-alphanumeric char, 
                   just output them both. */
                out_char('\\', userdata);
                if (*p) {
                    out_char(*p, userdata);
                    p++;
                }
            }
        } else {
            /* Normal character (not a backslash). */
            out_char(*p, userdata);
            p++;
        }
    }
}

// Public function that reads input, then parses and expands it
static void parse_and_expand(MacroTable *table, int argc, char *argv[]) {
    char *input = read_all_input_and_remove_comments(argc, argv);
    expand_text_impl(table, input, out_char_stdout, NULL);
    free(input);
}

// Main function to set up macro table, process input, expand macros,
// then free everything
int main(int argc, char *argv[]) {
    MacroTable table;
    init_macro_table(&table);

    parse_and_expand(&table, argc, argv);

    free_macro_table(&table);
    return 0;
}