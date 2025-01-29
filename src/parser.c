// File: parser.c

#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


// A function pointer type for outputting characters
typedef void (*output_char_fn)(char c, void *userdata);

// We'll use this struct for capturing text into a dynamic buffer
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} OutputBuffer;

/****************************************
 *       Output Helper Functions        *
 ****************************************/

static void out_char_stdout(char c, void *userdata) {
    (void)userdata; // unused
    putchar(c);
}

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


/**
 * Utility function to read all files (or stdin) into one large buffer.
 * Also removes single-line comments introduced by unescaped '%'.
 *
 * Returns a malloc'd buffer containing the cleaned text.
 * Caller must free when done.
 */
static char* read_all_input_and_remove_comments(int argc, char *argv[]) {
    // A simple approach: read everything into a dynamic buffer.
    // We'll guess we won't exceed some large size at once.
    // For a robust solution, you might do repeated resizing.

    size_t cap = 8192;
    size_t len = 0;
    char *buffer = malloc(cap);
    if (!buffer) {
        fprintf(stderr, "Error: malloc failed in read_all_input\n");
        exit(EXIT_FAILURE);
    }

    // Function to append a character to buffer
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
        } while(0)

    // If no files specified, read from stdin as if it's a single file
    if (argc < 2) {
        // Single pass approach:
        int c;
        int prevChar = 0;
        while ((c = fgetc(stdin)) != EOF) {
            // Check for comment start (unescaped '%')
            if (c == '%' && prevChar != '\\') {
                // skip until newline
                while ((c = fgetc(stdin)) != EOF && c != '\n');
                // keep the newline but remove everything until we find
                // a non-whitespace after the newline
                // For simplicity, let's just put the newline in buffer
                if (c != EOF) APPEND_CHAR('\n');
                prevChar = 0;
                continue;
            }
            APPEND_CHAR(c);
            prevChar = c;
        }
    } else {
        // Read each file in turn
        for (int i = 1; i < argc; i++) {
            FILE *fp = fopen(argv[i], "r");
            if (!fp) {
                fprintf(stderr, "Error: Cannot open file '%s'\n", argv[i]);
                exit(EXIT_FAILURE);
            }
            int c;
            int prevChar = 0;
            while ((c = fgetc(fp)) != EOF) {
                // Check for comment start
                if (c == '%' && prevChar != '\\') {
                    // skip until newline
                    while ((c = fgetc(fp)) != EOF && c != '\n');
                    if (c != EOF) APPEND_CHAR('\n');
                    prevChar = 0;
                    continue;
                }
                APPEND_CHAR(c);
                prevChar = c;
            }
            fclose(fp);
        }
    }

    // Null-terminate
    APPEND_CHAR('\0');
    return buffer;

    #undef APPEND_CHAR
}

/**
 * read_included_file:
 *   Given a path to a file, opens it and reads it into a buffer,
 *   removing comments (just like your main input). Returns a
 *   malloc'd string containing the cleaned text. Exits on error.
 */
char *read_included_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", path);
        exit(EXIT_FAILURE);
    }

    // We'll do a simple dynamic buffer approach
    size_t cap = 8192;
    size_t len = 0;
    char *buffer = malloc(cap);
    if (!buffer) {
        fprintf(stderr, "Error: malloc failed in read_included_file\n");
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
        } while(0)

    int c, prev = 0;
    while ((c = fgetc(fp)) != EOF) {
        // If we see '%' and it's not escaped by '\', skip until newline
        if (c == '%' && prev != '\\') {
            // skip to newline
            while ((c = fgetc(fp)) != EOF && c != '\n');
            if (c != EOF) {
                // put the newline in the buffer
                APPEND_CHAR('\n');
            }
            prev = 0;
            continue;
        }

        APPEND_CHAR(c);
        prev = c;
    }

    fclose(fp);
    APPEND_CHAR('\0');  // null-terminate

    #undef APPEND_CHAR
    return buffer;
}


/****************************************
 *     Forward declarations             *
 ****************************************/

static void expand_text_impl(MacroTable *table,
                             const char *input,
                             output_char_fn out_char,
                             void *userdata);

static void output_string(const char *s,
                          output_char_fn out_char,
                          void *userdata);

char *expand_text_into_string(MacroTable *table, 
                            const char *input);

char *combine_strings(const char *s1, const char *s2);

/****************************************
 *     Helper: Output a string          *
 ****************************************/

// We'll need this in places where we used to do `printf("%s", outBuf)`
static void output_string(const char *s,
                          output_char_fn out_char,
                          void *userdata)
{
    while (*s) {
        out_char(*s, userdata);
        s++;
    }
}


/**
 * Skeleton function to parse the big input string and do expansions.
 * We'll do:
 *   - look for macros: \...
 *   - if it's a built-in, handle it
 *   - if it's user-defined, expand with the single {arg}
 *   - otherwise output the text
 */
static void expand_text_impl(MacroTable *table,
                             const char *input,
                             output_char_fn out_char,
                             void *userdata)
{
    const char *p = input;
    while (*p != '\0') {
        if (*p == '\\') {
            p++; // skip the backslash
            // Check if next char is one of the special escapes
            if (*p == '\\' || *p == '{' || *p == '}' ||
                *p == '#' || *p == '%') {
                // It's an escaped char => just output it
                out_char(*p, userdata);  // <-- CHANGED (was putchar(*p))
                p++;
                continue;
            } else if (isalnum((unsigned char)*p)) {
                // parse macro name
                char nameBuf[256];
                int idx = 0;
                while (isalnum((unsigned char)*p) && idx < 255) {
                    nameBuf[idx++] = *p;
                    p++;
                }
                nameBuf[idx] = '\0';

                extern char *readArg(const char **pp);

                if (strcmp(nameBuf, "def") == 0) {
                    // \def{NAME}{VALUE}
                    char *arg1 = readArg(&p); // NAME
                    char *arg2 = readArg(&p); // VALUE
                    if (!arg1 || !*arg1) {
                        fprintf(stderr, "Error: invalid macro name in \\def\n");
                        exit(EXIT_FAILURE);
                    }
                    for (char *c = arg1; *c; c++) {
                        if (!isalnum((unsigned char)*c)) {
                            fprintf(stderr, "Error: invalid macro name in \\def\n");
                            exit(EXIT_FAILURE);
                        }
                    }
                    define_macro(table, arg1, arg2 ? arg2 : "");
                    free(arg1);
                    free(arg2);
                    // expands to empty => do nothing else
                    continue;
                }
                else if (strcmp(nameBuf, "undef") == 0) {
                    // \undef{NAME}
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
                    // \if{COND}{THEN}{ELSE}
                    char *condArg = readArg(&p);
                    char *thenArg = readArg(&p);
                    char *elseArg = readArg(&p);
                    if (!condArg || !thenArg || !elseArg) {
                        fprintf(stderr, "Error: \\if requires 3 arguments\n");
                        exit(EXIT_FAILURE);
                    }
                    const char *branch = (condArg[0] == '\0') ? elseArg : thenArg;
                    const char *remaining = p;

                    expand_text_impl(table, branch, out_char, userdata);  // <-- pass out_char
                    expand_text_impl(table, remaining, out_char, userdata);

                    free(condArg);
                    free(thenArg);
                    free(elseArg);
                    return; // end this invocation to avoid double processing
                }
                else if (strcmp(nameBuf, "ifdef") == 0) {
                    // \ifdef{NAME}{THEN}{ELSE}
                    char *nameArg = readArg(&p);
                    char *thenArg = readArg(&p);
                    char *elseArg = readArg(&p);
                    if (!nameArg || !thenArg || !elseArg) {
                        fprintf(stderr, "Error: \\ifdef requires 3 arguments\n");
                        exit(EXIT_FAILURE);
                    }
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
                    // \include{PATH}
                    char *pathArg = readArg(&p);
                    if (!pathArg) {
                        fprintf(stderr, "Error: \\include requires 1 argument\n");
                        exit(EXIT_FAILURE);
                    }
                    const char *remaining = p;
                    char *includedText = read_included_file(pathArg);

                    expand_text_impl(table, includedText, out_char, userdata);
                    expand_text_impl(table, remaining, out_char, userdata);

                    free(includedText);
                    free(pathArg);
                    return;
                }
                else if (strcmp(nameBuf, "expandafter") == 0) {
                    // \expandafter{BEFORE}{AFTER}
                    char *beforeArg = readArg(&p);
                    char *afterArg  = readArg(&p);
                    if (!beforeArg || !afterArg) {
                        fprintf(stderr, "Error: \\expandafter requires 2 arguments\n");
                        exit(EXIT_FAILURE);
                    }
                    const char *remaining = p;

                    // Step 1: fully expand afterArg into a string
                    char *expandedAfter = expand_text_into_string(table, afterArg);

                    // Step 2: combine BEFORE + expandedAfter
                    char *combined = combine_strings(beforeArg, expandedAfter);

                    // Step 3: parse combined from the start
                    expand_text_impl(table, combined, out_char, userdata);

                    // Step 4: expand leftover
                    expand_text_impl(table, remaining, out_char, userdata);

                    free(beforeArg);
                    free(afterArg);
                    free(expandedAfter);
                    free(combined);
                    return;
                }
                else {
                    // user-defined macro or unknown built-in
                    char *macroVal = lookup_macro(table, nameBuf);
                    if (!macroVal) {
                        fprintf(stderr, "Error: Macro '%s' not defined or not implemented\n", nameBuf);
                        exit(EXIT_FAILURE);
                    }
                    // user-defined macros have 1 arg
                    char *arg1 = readArg(&p);

                    // Expand the macro by substituting '#'
                    // The code to handle backslash escapes remains the same,
                    // but we'll output using out_char.
                    const char *m = macroVal;
                    size_t outCap = 256;
                    size_t outLen = 0;
                    char *outBuf = malloc(outCap);
                    if (!outBuf) exit(EXIT_FAILURE);

                    while (*m) {
                        if (*m == '\\') {
                            char next = *(m + 1);
                            if (next == '\0') {
                                // backslash at end
                                outBuf[outLen++] = '\\';
                                m++;
                            }
                            else if (next == '\\' || next == '{' || next == '}' ||
                                     next == '#' || next == '%') {
                                outBuf[outLen++] = next;
                                m += 2;
                            }
                            else if (isalnum((unsigned char) next)) {
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
                            if (arg1) {
                                size_t argLen = strlen(arg1);
                                while (outLen + argLen + 1 >= outCap) {
                                    outCap *= 2;
                                    outBuf = realloc(outBuf, outCap);
                                    if (!outBuf) exit(EXIT_FAILURE);
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
                            outBuf = realloc(outBuf, outCap);
                            if (!outBuf) exit(EXIT_FAILURE);
                        }
                    }
                    outBuf[outLen] = '\0';

                    // Now we have the expanded macro body in outBuf
                    // We parse it again to allow nested macros, but for a quick approach:
                    // we just output it. Let's do output_string with out_char.
                    output_string(outBuf, out_char, userdata);

                    free(outBuf);
                    if (arg1) free(arg1);
                    continue;
                }
            } else {
                // A backslash followed by something that isn't recognized => output literally
                out_char('\\', userdata);  // <-- was putchar
                if (*p) {
                    out_char(*p, userdata);
                    p++;
                }
            }
        } else {
            // Normal character => output
            out_char(*p, userdata);
            p++;
        }
    }
}


/****************************************
 *   Public Wrappers for expand_text    *
 ****************************************/

// 1) Expand and output to stdout
static void expand_text_to_stdout(MacroTable *table, const char *input) {
    expand_text_impl(table, input, out_char_stdout, NULL);
}

// 2) Expand into a malloc'd string
char *expand_text_into_string(MacroTable *table, const char *input) {
    // set up a buffer
    OutputBuffer ob;
    ob.len = 0;
    ob.cap = 256;
    ob.buf = malloc(ob.cap);
    if (!ob.buf) {
        fprintf(stderr, "Error: malloc failed in expand_text_into_string\n");
        exit(EXIT_FAILURE);
    }
    ob.buf[0] = '\0';

    // parse with the buffer-based writer
    expand_text_impl(table, input, out_char_buffer, &ob);

    // return the captured string
    return ob.buf; // caller must free
}

/****************************************
 *     Helper to combine strings        *
 ****************************************/

char *combine_strings(const char *s1, const char *s2) {
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


/**
 * readArg: reads a brace-balanced argument from the input.
 *   e.g. if *pp = "{stuff {nested} }...rest...", after calling readArg,
 *   we return "stuff {nested} " (without braces), and *pp moves past it.
 *
 *   If there's no '{' or it's not balanced, returns NULL or triggers error.
 */
char *readArg(const char **pp) {
    // Skip any whitespace (if needed)
    while (isspace((unsigned char)**pp)) {
        (*pp)++;
    }
    if (**pp != '{') {
        return NULL; // not an argument
    }
    (*pp)++; // skip '{'

    // We’ll read until we find the matching '}', accounting for nesting.
    // Note: we must watch out for escaped braces, e.g. '\{' doesn't count as opening brace.

    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) exit(EXIT_FAILURE);

    int braceDepth = 1;
    int prevChar = 0;

    while (**pp && braceDepth > 0) {
        char c = **pp;
        (*pp)++;
        if (c == '{' && prevChar != '\\') {
            braceDepth++;
            buf[len++] = c;
        } else if (c == '}' && prevChar != '\\') {
            braceDepth--;
            if (braceDepth > 0) {
                buf[len++] = c;
            }
        } else {
            buf[len++] = c;
        }
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) exit(EXIT_FAILURE);
        }
        prevChar = c;
    }

    if (braceDepth != 0) {
        free(buf);
        fprintf(stderr, "Error: unbalanced braces in argument\n");
        exit(EXIT_FAILURE);
    }

    // Null-terminate
    buf[len] = '\0';

    return buf;
}

/*******************************
 * Public function in parser.h *
 *******************************/
void parse_and_expand(MacroTable *table, int argc, char *argv[]) {
    char *input = read_all_input_and_remove_comments(argc, argv);

    // Instead of calling the old expand_text, we call:
    expand_text_to_stdout(table, input);  // prints expansions to stdout

    free(input);
}
