// File: parser.c

#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
 * Skeleton function to parse the big input string and do expansions.
 * We'll do:
 *   - look for macros: \...
 *   - if it's a built-in, handle it
 *   - if it's user-defined, expand with the single {arg}
 *   - otherwise output the text
 */
static void expand_text(MacroTable *table, const char *input) {
    // We'll do a simple approach: parse char-by-char, looking for backslash
    // This is very simplified and won't handle ALL edge cases.

    const char *p = input;
    while (*p != '\0') {
        if (*p == '\\') {
            // Could be a macro or an escape
            const char *macroStart = p;
            p++; // skip the backslash

            // Check if next char is one of the special escapes: \, {, }, #, %
            // Or if it’s alphanumeric => macro
            if (*p == '\\' || *p == '{' || *p == '}' || *p == '#' || *p == '%') {
                // It's an escaped character => just print the character, skip the backslash
                putchar(*p);
                p++;
                continue;
            } else if (isalnum((unsigned char)*p)) {
                // We have a macro name
                // collect the name
                char nameBuf[256];
                int idx = 0;
                while (isalnum((unsigned char)*p) && idx < 255) {
                    nameBuf[idx++] = *p;
                    p++;
                }
                nameBuf[idx] = '\0';

                // Now parse arguments in braces (for built-ins or user macros)
                // For example, \def{NAME}{VALUE}
                // For user macros, \NAME{ARG}

                // Check for built-in macros first:
                if (strcmp(nameBuf, "def") == 0) {
                    // parse \def{NAME}{VALUE}
                    // For brevity, let's parse 2 arguments
                    char *arg1 = NULL; // NAME
                    char *arg2 = NULL; // VALUE
                    // readArg is a function we’ll define soon
                    extern char *readArg(const char **pp); 
                    arg1 = readArg(&p);
                    arg2 = readArg(&p);

                    // Validate arg1 is alphanumeric and non-empty
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

                    // Define the macro
                    define_macro(table, arg1, arg2 ? arg2 : "");

                    // \def expands to empty
                    free(arg1);
                    free(arg2);
                    continue;
                } else if (strcmp(nameBuf, "undef") == 0) {
                    // parse \undef{NAME}
                    extern char *readArg(const char **pp);
                    char *arg1 = readArg(&p);
                    if (!arg1) {
                        fprintf(stderr, "Error: invalid \\undef usage\n");
                        exit(EXIT_FAILURE);
                    }
                    undef_macro(table, arg1);
                    free(arg1);
                    // expands to empty
                    continue;
                } else {
                    // It's either user-defined or another built-in (if, ifdef, include, etc.)
                    // Let's check if user-defined:
                    char *macroVal = lookup_macro(table, nameBuf);
                    if (!macroVal) {
                        // Not found => might be a built-in not yet implemented, or error
                        // For now, let's assume error
                        fprintf(stderr, "Error: Macro '%s' not defined or not implemented\n", nameBuf);
                        exit(EXIT_FAILURE);
                    }
                    // We have a user-defined macro that takes 1 argument:
                    extern char *readArg(const char **pp);
                    char *arg1 = readArg(&p);

                    // If the macroVal contains '#', we replace '#' with arg1
                    // Let's do a simple string replace
                    // We'll store the result in a dynamic buffer

                    const char *m = macroVal;
                    size_t outCap = 256;
                    size_t outLen = 0;
                    char *outBuf = malloc(outCap);
                    if (!outBuf) exit(EXIT_FAILURE);

                    while (*m) {
                        if (*m == '#' && arg1) {
                            // insert arg1
                            size_t argLen = strlen(arg1);
                            // ensure capacity
                            while (outLen + argLen + 1 >= outCap) {
                                outCap *= 2;
                                outBuf = realloc(outBuf, outCap);
                                if (!outBuf) exit(EXIT_FAILURE);
                            }
                            memcpy(outBuf + outLen, arg1, argLen);
                            outLen += argLen;
                            m++;
                        } else {
                            // copy normal char
                            if (outLen + 2 >= outCap) {
                                outCap *= 2;
                                outBuf = realloc(outBuf, outCap);
                                if (!outBuf) exit(EXIT_FAILURE);
                            }
                            outBuf[outLen++] = *m++;
                        }
                    }
                    outBuf[outLen] = '\0';

                    // Now we've expanded user macro into outBuf
                    // According to the assignment, “processing resumes at the start of the replacement string,”
                    // meaning we must parse outBuf *as if it’s next in the input*.
                    // A simple approach: recursively call expand_text on outBuf.
                    // But you’ll need a mechanism that merges it with the remainder of the text (p).
                    // For simplicity here, let's just print outBuf as is:
                    // In a real solution, you'd re-inject it into the parsing pipeline.

                    printf("%s", outBuf);

                    free(outBuf);
                    if (arg1) free(arg1);
                    continue;
                }
            } else {
                // A backslash followed by something that isn't alphanumeric or an escape => output literally
                putchar('\\');
                // do NOT advance p again (we only advanced once so far)
                // but we want to output the char at *p
                if (*p) {
                    putchar(*p);
                    p++;
                }
            }
        } else {
            // Normal character => output
            putchar(*p);
            p++;
        }
    }
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
    expand_text(table, input);
    free(input);
}
