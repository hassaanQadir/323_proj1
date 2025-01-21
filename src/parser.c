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
            // const char *macroStart = p;
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
                } else if (strcmp(nameBuf, "if") == 0) {
                    // We expect 3 arguments: {COND}, {THEN}, {ELSE}
                    extern char *readArg(const char **pp);

                    char *condArg = readArg(&p);   // the COND
                    char *thenArg = readArg(&p);   // the THEN
                    char *elseArg = readArg(&p);   // the ELSE

                    // Error out if any argument is missing
                    if (!condArg || !thenArg || !elseArg) {
                        fprintf(stderr, "Error: \\if requires 3 arguments\n");
                        exit(EXIT_FAILURE);
                    }

                    // Check if COND is empty (not expanded!)
                    // If condArg is "", that means false => use elseArg
                    // If condArg is non-empty => use thenArg
                    const char *branch = (condArg[0] == '\0') ? elseArg : thenArg;

                    // 1) "Replace" the \if macro with the chosen branch
                    // 2) Then we need to parse/expand that chosen branch
                    //    so that macros within it are handled.
                    
                    // Easiest approach: use your existing expand_text
                    // But we also have leftover text in 'p' after this macro.
                    // We'll do a small trick: 
                    //
                    //    - expand 'branch' into a temporary buffer or directly by calling expand_text
                    //    - print or store that expansion
                    //    - continue 'expand_text' after this macro is done
                    // 
                    // For a quick approach, let's do an inline sub-call:

                    // We'll store the remainder (p) in a temp variable:
                    const char *remaining = p;

                    // We'll parse branch first, then parse remaining second. 
                    // But we don't want to lose our recursion logic.
                    // A naive solution: 
                    //   - call expand_text(table, branch)
                    //   - then call expand_text(table, remaining)
                    //   - return from this function (so we don't keep going char-by-char)
                    //
                    // Alternatively, you could "inject" branch into your output buffer, 
                    // then keep going from there. 
                    // We'll show the naive approach for clarity.

                    expand_text(table, branch);  // expand THEN or ELSE text
                    // Now expand the rest of the current string
                    expand_text(table, remaining);

                    // Free allocated arg strings
                    free(condArg);
                    free(thenArg);
                    free(elseArg);

                    // We must return here because we've effectively re-run expand_text 
                    // for the remainder. If we continued the current while loop, 
                    // we'd double-process the leftover text.
                    return;
                } else if (strcmp(nameBuf, "ifdef") == 0) {
                    // We expect 3 arguments: {NAME}, {THEN}, {ELSE}
                    extern char *readArg(const char **pp);

                    char *nameArg = readArg(&p);  // The macro name
                    char *thenArg = readArg(&p);
                    char *elseArg = readArg(&p);

                    // Check for missing arguments
                    if (!nameArg || !thenArg || !elseArg) {
                        fprintf(stderr, "Error: \\ifdef requires 3 arguments\n");
                        exit(EXIT_FAILURE);
                    }

                    // Validate nameArg is alphanumeric
                    for (char *c = nameArg; *c; c++) {
                        if (!isalnum((unsigned char)*c)) {
                            fprintf(stderr, "Error: invalid macro name in \\ifdef\n");
                            exit(EXIT_FAILURE);
                        }
                    }

                    // Condition: if nameArg is defined => use thenArg, else use elseArg
                    char *macroVal = lookup_macro(table, nameArg);
                    const char *branch = (macroVal != NULL) ? thenArg : elseArg;

                    // Same approach as for \if:
                    // We'll do a naive approach: expand 'branch', then expand the leftover text

                    const char *remaining = p; // store leftover text

                    expand_text(table, branch);   // expand the chosen branch
                    expand_text(table, remaining);

                    // Free
                    free(nameArg);
                    free(thenArg);
                    free(elseArg);

                    // Return to avoid double-processing
                    return;
                } else if (strcmp(nameBuf, "include") == 0) {
                    extern char *readArg(const char **pp);
                    char *pathArg = readArg(&p);  // parse the single {PATH} argument

                    if (!pathArg) {
                        fprintf(stderr, "Error: \\include requires 1 argument\n");
                        exit(EXIT_FAILURE);
                    }

                    // We'll read and parse the included file's text
                    // Then we expand it, then continue with the leftover input.

                    // leftover input after the macro
                    const char *remaining = p;

                    // Expand the file's contents 
                    char *includedText = read_included_file(pathArg);
                    expand_text(table, includedText);

                    // After finishing the included file, continue with the rest
                    expand_text(table, remaining);

                    // Clean up
                    free(includedText);
                    free(pathArg);

                    // Return so we don't double-process leftover text in this function
                    return;
                } else if (strcmp(nameBuf, "expandafter") == 0) {
                    extern char *readArg(const char **pp);

                    // Parse the 2 brace-balanced arguments: {BEFORE} and {AFTER}
                    char *beforeArg = readArg(&p);  // {BEFORE}
                    char *afterArg  = readArg(&p);  // {AFTER}

                    if (!beforeArg || !afterArg) {
                        fprintf(stderr, "Error: \\expandafter requires 2 arguments\n");
                        exit(EXIT_FAILURE);
                    }

                    // leftover text after \expandafter macro
                    const char *remaining = p;

                    // Step 1: Fully expand 'afterArg' as if it were alone
                    char *expandedAfter = expand_text_into_string(table, afterArg);

                    // Step 2: Create a buffer that concatenates BEFORE + expandedAfter
                    // We do NOT expand 'beforeArg' yet; we just place it as-is.
                    char *combined = combine_strings(beforeArg, expandedAfter);

                    // Step 3: Now parse 'combined' from the start.
                    // Because 'expand_text_into_string' is a helper that returns a fully expanded string,
                    // we want to treat 'beforeArg' as unexpanded macros + 'expandedAfter' as literal text
                    // that was already expanded. 
                    // But the assignment says "standard expansion processing should continue,
                    // starting from the start of BEFORE." 
                    // So we do a new call to expand_text, so any new macros in 'expandedAfter'
                    // can affect 'beforeArg'.

                    expand_text(table, combined);

                    // Step 4: Finally, expand the leftover text after \expandafter
                    expand_text(table, remaining);

                    // Clean up
                    free(beforeArg);
                    free(afterArg);
                    free(expandedAfter);
                    free(combined);

                    return; // done handling \expandafter
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

                    while (*m != '\0') {
                        if (*m == '\\') {
                            // Look ahead to the next character
                            char next = *(m + 1);

                            // If we're at the end of the string (backslash is the last char), just output '\'
                            if (next == '\0') {
                                outBuf[outLen++] = '\\';
                                m++;
                                continue;
                            }

                            // 1) If next is one of the special escapes: \, {, }, #, %
                            if (next == '\\' || next == '{' || next == '}' ||
                                next == '#' || next == '%') {
                                // "When it's time to output, ignore the backslash and output only the second char."
                                outBuf[outLen++] = next;
                                m += 2;  // skip "\X"
                            }
                            // 2) If next is alphanumeric => typically this is a macro start in the main parser,
                            //    but inside a macro *value*, you might want to treat it as literal or do a second pass.
                            else if (isalnum((unsigned char) next)) {
                                // Possibly a macro? In a simple approach, output them literally:
                                outBuf[outLen++] = '\\';
                                outBuf[outLen++] = next;
                                m += 2;
                            }
                            // 3) Otherwise, next is not special => output both
                            else {
                                outBuf[outLen++] = '\\';
                                outBuf[outLen++] = next;
                                m += 2;
                            }
                        }
                        else if (*m == '#') {
                            // This is an *unescaped* '#'. If your macro allows argument substitution:
                            if (arg1) {
                                // Insert arg1 in place of '#'
                                size_t argLen = strlen(arg1);
                                // expand buffer if needed
                                while (outLen + argLen + 1 >= outCap) {
                                    outCap *= 2;
                                    outBuf = realloc(outBuf, outCap);
                                    if (!outBuf) exit(EXIT_FAILURE);
                                }
                                memcpy(outBuf + outLen, arg1, argLen);
                                outLen += argLen;
                            }
                            m++; // skip the '#'
                        }
                        else {
                            // Normal character => just copy
                            outBuf[outLen++] = *m++;
                        }

                        // Expand buffer if needed
                        if (outLen + 1 >= outCap) {
                            outCap *= 2;
                            outBuf = realloc(outBuf, outCap);
                            if (!outBuf) exit(EXIT_FAILURE);
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
