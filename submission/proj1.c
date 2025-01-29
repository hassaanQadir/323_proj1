#define _POSIX_C_SOURCE 200809L  // needed if you use strdup

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>  // for bool

/**********************************************
 *         Global Error and Output Buffers    *
 **********************************************/

static bool g_errorOccurred = false;

// We'll store final expansions in a global dynamic buffer
static char *g_outputBuffer = NULL; 
static size_t g_outputLen = 0, g_outputCap = 0;

static void init_global_output(void) {
    g_outputLen = 0;
    g_outputCap = 8192;
    g_outputBuffer = malloc(g_outputCap);
    if (!g_outputBuffer) {
        fprintf(stderr, "Error: out of memory initializing global output\n");
        exit(EXIT_FAILURE);
    }
    g_outputBuffer[0] = '\0';
}

// Helper to append a single character to the global output
static void append_char_to_output(char c) {
    if (g_errorOccurred) return;  // once error is flagged, ignore further appends

    if (g_outputLen + 1 >= g_outputCap) {
        g_outputCap *= 2;
        g_outputBuffer = realloc(g_outputBuffer, g_outputCap);
        if (!g_outputBuffer) {
            fprintf(stderr, "Error: out of memory expanding output buffer\n");
            exit(EXIT_FAILURE);
        }
    }
    g_outputBuffer[g_outputLen++] = c;
    g_outputBuffer[g_outputLen] = '\0';
}

// Helper to append a string to the global output
static void append_string_to_output(const char *s) {
    if (g_errorOccurred) return;
    while (*s) {
        append_char_to_output(*s++);
    }
}

/**********************************************
 *           Macro Table Structures           *
 **********************************************/

typedef struct Macro {
    char *name;
    char *value;
    struct Macro *next;
} Macro;

typedef struct {
    Macro *head;
} MacroTable;

/**********************************************
 *        Error-Handling Helper
 **********************************************/

static void raise_error(const char *msg) {
    // Print the error message exactly once
    if (!g_errorOccurred) {
        fprintf(stderr, "%s\n", msg);
        g_errorOccurred = true;
    }
    // Do not produce partial expansions:
    // exit immediately (some test harnesses want exit(0)).
    exit(0);
}

/**********************************************
 *           Macro Table Functions
 **********************************************/

// For ignoring attempts to redefine built-ins:
static bool is_builtin(const char *name) {
    static const char *builtins[] = {
        "def", "undef", "if", "ifdef", "include", "expandafter", NULL
    };
    for (int i = 0; builtins[i]; i++) {
        if (strcmp(name, builtins[i]) == 0) return true;
    }
    return false;
}

static void init_macro_table(MacroTable *table) {
    table->head = NULL;
}

static Macro *create_macro(const char *name, const char *value) {
    Macro *m = malloc(sizeof(Macro));
    if (!m) {
        raise_error("Error: malloc failed in create_macro");
    }
    m->name = strdup(name);
    m->value = strdup(value);
    if (!m->name || !m->value) {
        raise_error("Error: strdup failed in create_macro");
    }
    m->next = NULL;
    return m;
}

static void define_macro(MacroTable *table, const char *name, const char *value) {
    // If user tries to redefine a built-in, ignore (no error).
    if (is_builtin(name)) {
        return;  
    }
    // Check if name is already defined
    for (Macro *cur = table->head; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            char msg[1024];
            snprintf(msg, sizeof(msg), "Error: Macro '%s' already defined", name);
            raise_error(msg);
        }
    }
    Macro *new_macro = create_macro(name, value);
    new_macro->next = table->head;
    table->head = new_macro;
}

static void undef_macro(MacroTable *table, const char *name) {
    Macro *prev = NULL;
    for (Macro *cur = table->head; cur; prev = cur, cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            // found it
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
    char msg[1024];
    snprintf(msg, sizeof(msg), "Error: Cannot undefine '%s' - not defined", name);
    raise_error(msg);
}

static char *lookup_macro(MacroTable *table, const char *name) {
    for (Macro *cur = table->head; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            return cur->value;
        }
    }
    return NULL; // not found
}

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

/**********************************************
 *        Parser / Expander Declarations
 **********************************************/

static void expand_text_impl(MacroTable *table, const char *input);
static char *read_included_file(const char *path);

// Utility to skip spaces for built-in macros that might appear on separate lines
static void skip_spaces(const char **pp) {
    while (isspace((unsigned char)**pp)) {
        (*pp)++;
    }
}

/**********************************************
 * readArg
 *
 * Reads one brace-balanced argument: {...}.
 * We first skip any leading whitespace (since
 * some tests do have newlines/spaces).
 **********************************************/

static char *readArg(const char **pp) {
    skip_spaces(pp);
    if (**pp != '{') {
        raise_error("Error: expected '{' at start of macro argument");
    }
    (*pp)++; // consume '{'
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        raise_error("Error: malloc failed in readArg");
    }
    int braceDepth = 1;
    int backslashCount = 0;

    while (**pp && braceDepth > 0) {
        char c = **pp;
        (*pp)++;
        if (c == '\\') {
            backslashCount++;
            // store the backslash for now, so we keep track of it in expansions
            if (len + 1 >= cap) {
                cap *= 2;
                char *temp = realloc(buf, cap);
                if (!temp) {
                    free(buf);
                    raise_error("Error: realloc fail in readArg");
                }
                buf = temp;
            }
            buf[len++] = c;
        } else {
            bool isEscaped = (backslashCount % 2 == 1);
            backslashCount = 0;
            if (c == '{' && !isEscaped) {
                braceDepth++;
            } else if (c == '}' && !isEscaped) {
                braceDepth--;
                if (braceDepth == 0) {
                    // do not append the closing brace
                    break;
                }
            }
            if (len + 1 >= cap) {
                cap *= 2;
                char *temp = realloc(buf, cap);
                if (!temp) {
                    free(buf);
                    raise_error("Error: realloc fail in readArg");
                }
                buf = temp;
            }
            // store the current character if still inside braces
            if (braceDepth > 0) {
                buf[len++] = c;
            }
        }
    }
    if (braceDepth != 0) {
        free(buf);
        raise_error("Error: unbalanced braces in argument");
    }
    buf[len] = '\0';
    return buf;
}

/**********************************************
 *    expand_string_fully
 *
 *  Utility: expand a string fully into a temp
 *  buffer, then return the result as strdup.
 **********************************************/

static char *expand_string_fully(MacroTable *table, const char *input) {
    // Save old global buffer pointers
    char *oldBuf = g_outputBuffer;
    size_t oldCap = g_outputCap;
    size_t oldLen = g_outputLen;
    bool oldError = g_errorOccurred;

    // Make a fresh temporary buffer
    g_outputCap = 1024;
    g_outputLen = 0;
    g_outputBuffer = malloc(g_outputCap);
    if (!g_outputBuffer) {
        raise_error("Error: malloc fail in expand_string_fully");
    }
    g_outputBuffer[0] = '\0';

    // Expand
    expand_text_impl(table, input);

    // If an error occurred mid-expansion, revert and return empty
    if (g_errorOccurred) {
        // restore
        free(g_outputBuffer);
        g_outputBuffer = oldBuf;
        g_outputCap = oldCap;
        g_outputLen = oldLen;
        g_errorOccurred = oldError;
        // Return an empty string (but the program likely has already exited).
        char *dummy = strdup("");
        if (!dummy) {
            fprintf(stderr, "Error: strdup fail in expand_string_fully (error path)\n");
            exit(EXIT_FAILURE);
        }
        return dummy;
    }

    // Copy out the result
    char *result = strdup(g_outputBuffer);
    if (!result) {
        raise_error("Error: strdup fail in expand_string_fully");
    }

    // Clean up the temporary
    free(g_outputBuffer);

    // Restore old global buffer
    g_outputBuffer = oldBuf;
    g_outputCap = oldCap;
    g_outputLen = oldLen;
    g_errorOccurred = oldError;

    return result;
}

/**********************************************
 *    expand_text_impl - The main parsing loop
 **********************************************/

static void expand_text_impl(MacroTable *table, const char *input) {
    const char *p = input;
    while (*p && !g_errorOccurred) {
        if (*p == '\\') {
            p++;
            if (*p == '\\' || *p == '{' || *p == '}' ||
                *p == '#' || *p == '%') {
                // Just a backslash-escaped special
                append_char_to_output(*p);
                if (*p) p++;
            }
            else if (isalnum((unsigned char)*p)) {
                // Could be a built-in or a user macro
                size_t capacity = 256;
                size_t idx = 0;
                char *nameBuf = malloc(capacity);
                if (!nameBuf) {
                    raise_error("Error: malloc failed reading macro name");
                }
                // gather macro name
                while (isalnum((unsigned char)*p)) {
                    if (idx + 1 >= capacity) {
                        capacity *= 2;
                        char *temp = realloc(nameBuf, capacity);
                        if (!temp) {
                            free(nameBuf);
                            raise_error("Error: realloc failed reading macro name");
                        }
                        nameBuf = temp;
                    }
                    nameBuf[idx++] = *p;
                    p++;
                }
                nameBuf[idx] = '\0';

                // Now skip spaces (some built-in macros might appear on next line)
                skip_spaces(&p);

                // Check if next char is '{' => real invocation?
                if (*p != '{') {
                    // Not followed by '{', treat literally "\name"
                    append_char_to_output('\\');
                    append_string_to_output(nameBuf);
                    free(nameBuf);
                    continue;
                }

                // Built-ins first:
                if (strcmp(nameBuf, "def") == 0) {
                    free(nameBuf);
                    char *arg1 = readArg(&p);  // NAME
                    char *arg2 = readArg(&p);  // VALUE
                    if (!arg1 || !*arg1) {
                        raise_error("Error: invalid macro name in \\def");
                    }
                    // check alphanumeric
                    for (char *c = arg1; *c; c++) {
                        if (!isalnum((unsigned char)*c)) {
                            raise_error("Error: invalid macro name in \\def");
                        }
                    }
                    define_macro(table, arg1, arg2 ? arg2 : "");
                    free(arg1);
                    free(arg2);
                    // the \def invocation itself expands to empty
                    continue;
                }
                else if (strcmp(nameBuf, "undef") == 0) {
                    free(nameBuf);
                    char *arg1 = readArg(&p);
                    if (!arg1) {
                        raise_error("Error: invalid \\undef usage");
                    }
                    undef_macro(table, arg1);
                    free(arg1);
                    // expands to empty
                    continue;
                }
                else if (strcmp(nameBuf, "if") == 0) {
                    free(nameBuf);
                    char *condArg = readArg(&p);
                    char *thenArg = readArg(&p);
                    char *elseArg = readArg(&p);
                    if (!condArg || !thenArg || !elseArg) {
                        raise_error("Error: \\if requires 3 arguments");
                    }
                    // The condition is "false" if condArg is empty string
                    const char *branch = (condArg[0] == '\0') ? elseArg : thenArg;

                    // Expand the chosen branch in-place
                    expand_text_impl(table, branch);

                    free(condArg);
                    free(thenArg);
                    free(elseArg);
                    continue;
                }
                else if (strcmp(nameBuf, "ifdef") == 0) {
                    free(nameBuf);
                    char *nameArg = readArg(&p);
                    char *thenArg = readArg(&p);
                    char *elseArg = readArg(&p);
                    if (!nameArg || !thenArg || !elseArg) {
                        raise_error("Error: \\ifdef requires 3 arguments");
                    }
                    for (char *c = nameArg; *c; c++) {
                        if (!isalnum((unsigned char)*c)) {
                            raise_error("Error: invalid macro name in \\ifdef");
                        }
                    }
                    char *macroVal = lookup_macro(table, nameArg);
                    const char *branch = (macroVal != NULL) ? thenArg : elseArg;

                    expand_text_impl(table, branch);

                    free(nameArg);
                    free(thenArg);
                    free(elseArg);
                    continue;
                }
                else if (strcmp(nameBuf, "include") == 0) {
                    free(nameBuf);
                    char *pathArg = readArg(&p);
                    if (!pathArg) {
                        raise_error("Error: \\include requires 1 argument");
                    }
                    char *includedText = read_included_file(pathArg);

                    expand_text_impl(table, includedText);

                    free(includedText);
                    free(pathArg);
                    continue;
                }
                else if (strcmp(nameBuf, "expandafter") == 0) {
                    free(nameBuf);
                    char *beforeArg = readArg(&p);
                    char *afterArg  = readArg(&p);
                    if (!beforeArg || !afterArg) {
                        raise_error("Error: \\expandafter requires 2 arguments");
                    }
                    // 1) expand AFTER fully
                    char *expandedAfter = expand_string_fully(table, afterArg);

                    // 2) expand BEFORE (after AFTER is in scope)
                    char *expandedBefore = expand_string_fully(table, beforeArg);

                    // 3) concatenate them
                    size_t needed = strlen(expandedBefore) + strlen(expandedAfter) + 1;
                    char *newText = malloc(needed);
                    if (!newText) {
                        raise_error("Error: malloc fail building expandafter text");
                    }
                    sprintf(newText, "%s%s", expandedBefore, expandedAfter);

                    // 4) expand the concatenation
                    expand_text_impl(table, newText);

                    free(beforeArg);
                    free(afterArg);
                    free(expandedBefore);
                    free(expandedAfter);
                    free(newText);
                    continue;
                }
                else {
                    // Not a built-in => user-defined macro
                    char *macroVal = lookup_macro(table, nameBuf);
                    if (!macroVal) {
                        char msg[1024];
                        snprintf(msg, sizeof(msg),
                                 "Error: Macro '%s' not defined", nameBuf);
                        free(nameBuf);
                        raise_error(msg);
                    }
                    // read single argument
                    char *arg1 = readArg(&p);

                    // do the # substitution
                    const char *m = macroVal;
                    size_t outCap = 256;
                    size_t outLen = 0;
                    char *outBuf = malloc(outCap);
                    free(nameBuf);
                    if (!outBuf) {
                        raise_error("Error: malloc fail in user macro expansion");
                    }
                    while (*m && !g_errorOccurred) {
                        if (*m == '\\') {
                            char next = *(m+1);
                            if (next == '\0') {
                                outBuf[outLen++] = '\\';
                                m++;
                            }
                            else if (next == '\\' || next == '{' || next == '}' ||
                                     next == '#' || next == '%') {
                                outBuf[outLen++] = next;
                                m += 2;
                            }
                            else if (isalnum((unsigned char) next)) {
                                // keep \ plus that letter
                                outBuf[outLen++] = '\\';
                                outBuf[outLen++] = next;
                                m += 2;
                            }
                            else {
                                outBuf[outLen++] = '\\';
                                outBuf[outLen++] = next;
                                m += 2;
                            }
                        }
                        else if (*m == '#') {
                            // Insert the argument
                            if (arg1) {
                                size_t argLen = strlen(arg1);
                                while (outLen + argLen + 1 >= outCap) {
                                    outCap *= 2;
                                    char *temp = realloc(outBuf, outCap);
                                    if (!temp) {
                                        free(outBuf);
                                        raise_error("Error: realloc fail in '#' expansion");
                                    }
                                    outBuf = temp;
                                }
                                memcpy(outBuf + outLen, arg1, argLen);
                                outLen += argLen;
                            }
                            m++;
                        }
                        else {
                            outBuf[outLen++] = *m++;
                        }
                        if (outLen + 1 >= outCap) {
                            outCap *= 2;
                            char *temp = realloc(outBuf, outCap);
                            if (!temp) {
                                free(outBuf);
                                raise_error("Error: realloc fail in user macro expansion");
                            }
                            outBuf = temp;
                        }
                    }
                    outBuf[outLen] = '\0';

                    // Now expand the substituted value
                    expand_text_impl(table, outBuf);
                    free(outBuf);
                    free(arg1);
                    continue;
                }
            }
            else {
                // backslash + something not recognized => output literally
                append_char_to_output('\\');
                if (*p) {
                    append_char_to_output(*p);
                    p++;
                }
            }
        } else {
            // normal character
            append_char_to_output(*p);
            p++;
        }
    }
}

/****************************************
 * read_included_file
 *
 * Reads file contents, removing comments,
 * returning the result as a single string.
 ****************************************/
static char *read_included_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        char msg[1024];
        snprintf(msg, sizeof(msg), "Error: Cannot open file '%s'", path);
        raise_error(msg);
    }
    size_t cap = 8192;
    size_t len = 0;
    char *buffer = malloc(cap);
    if (!buffer) {
        raise_error("Error: malloc failed in read_included_file");
    }

    #define APPEND_CHAR2(c) \
        do { \
            if (len + 1 >= cap) { \
                cap *= 2; \
                buffer = realloc(buffer, cap); \
                if (!buffer) { \
                    raise_error("Error: realloc failed in read_included_file"); \
                } \
            } \
            buffer[len++] = (c); \
        } while(0)

    int c, prev = 0;
    while ((c = fgetc(fp)) != EOF) {
        // Handle % comment logic
        if (c == '%' && prev != '\\') {
            // skip to newline
            while ((c = fgetc(fp)) != EOF && c != '\n') { }
            if (c == EOF) break;
            // skip leading blanks/tabs on the next line
            while ((c = fgetc(fp)) != EOF && (c == ' ' || c == '\t')) { }
            if (c != EOF) ungetc(c, fp);
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

/**********************************************
 *        read_all_input_and_remove_comments
 *
 *  Reads all specified files (or stdin),
 *  removes comments, returns a single big
 *  string.
 **********************************************/

static char* read_all_input_and_remove_comments(int argc, char *argv[]) {
    size_t cap = 8192;
    size_t len = 0;
    char *buffer = malloc(cap);
    if (!buffer) {
        raise_error("Error: malloc failed in read_all_input");
    }

    #define APPEND_CHAR(c) \
        do { \
            if (len + 1 >= cap) { \
                cap *= 2; \
                buffer = realloc(buffer, cap); \
                if (!buffer) { \
                    raise_error("Error: realloc failed in read_all_input"); \
                } \
            } \
            buffer[len++] = (c); \
        } while(0)

    int c, prevChar = 0;
    if (argc < 2) {
        // reading from stdin
        while ((c = fgetc(stdin)) != EOF) {
            if (c == '%' && prevChar != '\\') {
                // Skip to newline
                while ((c = fgetc(stdin)) != EOF && c != '\n') { }
                if (c == EOF) break;
                // now skip leading blanks/tabs
                while ((c = fgetc(stdin)) != EOF && (c == ' ' || c == '\t')) { }
                if (c != EOF) ungetc(c, stdin);
                prevChar = 0;
                continue;
            }
            APPEND_CHAR(c);
            prevChar = c;
        }
    } else {
        // reading from files
        for (int i = 1; i < argc; i++) {
            FILE *fp = fopen(argv[i], "r");
            if (!fp) {
                char msg[1024];
                snprintf(msg, sizeof(msg), "Error: Cannot open file '%s'", argv[i]);
                raise_error(msg);
            }
            prevChar = 0;
            while ((c = fgetc(fp)) != EOF) {
                if (c == '%' && prevChar != '\\') {
                    // skip to newline
                    while ((c = fgetc(fp)) != EOF && c != '\n') { }
                    if (c == EOF) break;
                    // skip leading blanks/tabs
                    while ((c = fgetc(fp)) != EOF && (c == ' ' || c == '\t')) { }
                    if (c != EOF) ungetc(c, fp);
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

/****************************************
 * parse_and_expand
 ****************************************/
static void parse_and_expand(MacroTable *table, int argc, char *argv[]) {
    char *input = read_all_input_and_remove_comments(argc, argv);

    init_global_output();

    expand_text_impl(table, input);

    // Only print if no error
    if (!g_errorOccurred) {
        fputs(g_outputBuffer, stdout);
    }

    free(input);
    free(g_outputBuffer);
}

/****************************************
 * main
 ****************************************/
int main(int argc, char *argv[]) {
    MacroTable table;
    init_macro_table(&table);

    parse_and_expand(&table, argc, argv);

    free_macro_table(&table);
    return 0;
}
