#define _POSIX_C_SOURCE 200809L  // needed if you use strdup in some environments

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

/**********************************************
 *               Macro Table Code            *
 **********************************************/

typedef struct Macro {
    char *name;
    char *value;
    struct Macro *next; // singly-linked list
} Macro;

typedef struct {
    Macro *head;
} MacroTable;

static void init_macro_table(MacroTable *table) {
    table->head = NULL;
}

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

static void define_macro(MacroTable *table, const char *name, const char *value) {
    // check if already defined
    for (Macro *cur = table->head; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            fprintf(stderr, "Error: Macro '%s' already defined\n", name);
            exit(EXIT_FAILURE);
        }
    }
    // insert new macro at head
    Macro *new_macro = create_macro(name, value);
    new_macro->next = table->head;
    table->head = new_macro;
}

static void undef_macro(MacroTable *table, const char *name) {
    Macro *prev = NULL, *cur = table->head;
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
 *         Parser / Expander Code            *
 **********************************************/

typedef void (*output_char_fn)(char c, void *userdata);

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} OutputBuffer;

static void expand_text_impl(MacroTable *table, const char *input,
                             output_char_fn out_char, void *userdata);
static char *expand_text_into_string(MacroTable *table, const char *input);
static char *combine_strings(const char *s1, const char *s2);
static char *read_included_file(const char *path);

// Forward declaration of the improved readArg
static char *readArg(const char **pp);

/****************************************
 *  Output Helper Functions 
 ****************************************/

static void out_char_stdout(char c, void *userdata) {
    (void)userdata; 
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

/****************************************
 *  read_all_input_and_remove_comments 
 ****************************************/

static char* read_all_input_and_remove_comments(int argc, char *argv[]) {
    size_t cap = 8192;
    size_t len = 0;
    char *buffer = malloc(cap);
    if (!buffer) {
        fprintf(stderr, "Error: malloc failed in read_all_input\n");
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

    int c, prevChar = 0;

    if (argc < 2) {
        // read from stdin
        while ((c = fgetc(stdin)) != EOF) {
            if (c == '%' && prevChar != '\\') {
                // skip until newline
                while ((c = fgetc(stdin)) != EOF && c != '\n')
                    ;
                // skip leading blanks/tabs on next line
                if (c == '\n') {
                    int next_c;
                    while ((next_c = fgetc(stdin)) != EOF && (next_c == ' ' || next_c == '\t'))
                        ;
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
                        while ((next_c = fgetc(fp)) != EOF && (next_c == ' ' || next_c == '\t'))
                            ;
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

/****************************************
 * read_included_file
 ****************************************/

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
    } while(0)

    int c, prev = 0;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '%' && prev != '\\') {
            while ((c = fgetc(fp)) != EOF && c != '\n')
                ;
            if (c == '\n') {
                int next_c;
                while ((next_c = fgetc(fp)) != EOF && (next_c == ' ' || next_c == '\t'))
                    ;
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

/****************************************
 * expand_text_into_string
 ****************************************/

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

/****************************************
 * combine_strings
 ****************************************/

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

/****************************************
 * readArg (Improved)
 ****************************************/

// Parses a single macro argument of the form { ... }.
// Inside the braces, \{ and \} do *not* count towards braceDepth,
// so that things like \escape{\{} are valid.
static char *readArg(const char **pp) {
    // Skip whitespace first
    while (isspace((unsigned char)**pp)) {
        (*pp)++;
    }
    // Must start with '{'
    if (**pp != '{') {
        return NULL; 
    }
    (*pp)++; // consume '{'

    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        fprintf(stderr, "Error: malloc failed in readArg\n");
        exit(EXIT_FAILURE);
    }

    int braceDepth = 1; 
    bool escaped = false;  // are we in the middle of a backslash escape?

    while (**pp && braceDepth > 0) {
        char c = **pp;
        (*pp)++;

        if (!escaped) {
            if (c == '\\') {
                // Start an escape
                escaped = true;
                // Store the '\' literally
                buf[len++] = c;
            }
            else if (c == '{') {
                braceDepth++;
                buf[len++] = c;
            }
            else if (c == '}') {
                braceDepth--;
                if (braceDepth > 0) {
                    buf[len++] = c;
                }
            }
            else {
                buf[len++] = c;
            }
        } 
        else {
            // previous char was a '\', so treat this literally
            buf[len++] = c;
            escaped = false;
        }

        // Expand buffer if needed
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

/****************************************
 * readMacroName (FIX #1: dynamic reading)
 ****************************************/
static char *readMacroName(const char **pp)
{
    // We assume we've already seen the '\' and know *pp points to an
    // alphanumeric character (or we will check inside). We read
    // while isalnum(*pp).
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        fprintf(stderr, "Error: malloc failed in readMacroName\n");
        exit(EXIT_FAILURE);
    }

    // read while next char is alnum
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

/****************************************
 * expand_text_impl - The Core Parser
 ****************************************/

static void expand_text_impl(MacroTable *table, const char *input,
                             output_char_fn out_char, void *userdata)
{
    const char *p = input;
    while (*p != '\0') {
        if (*p == '\\') {
            // We have a leading backslash
            p++;
            // Check if we ran off the end
            if (*p == '\0') {
                // A trailing '\' at end of file => just output it
                out_char('\\', userdata);
                break;
            }

            // If next char is one of the "special 5" (\, {, }, #, %),
            // then per spec we output only that second char.
            if (*p == '\\' || *p == '{' || *p == '}' ||
                *p == '#' || *p == '%')
            {
                out_char(*p, userdata);
                p++;
                continue;
            }
            else if (isalnum((unsigned char)*p)) {
                // (FIX #2) Use the new dynamic readMacroName function.
                char *nameBuf = readMacroName(&p);

                // handle built-ins vs user-defined
                if (strcmp(nameBuf, "def") == 0) {
                    free(nameBuf);
                    char *arg1 = readArg(&p); // NAME
                    char *arg2 = readArg(&p); // VALUE
                    if (!arg1 || !*arg1) {
                        fprintf(stderr, "Error: invalid macro name in \\def\n");
                        exit(EXIT_FAILURE);
                    }
                    // check all alnum
                    for (char *c = arg1; *c; c++) {
                        if (!isalnum((unsigned char)*c)) {
                            fprintf(stderr, "Error: invalid macro name in \\def\n");
                            exit(EXIT_FAILURE);
                        }
                    }
                    define_macro(table, arg1, arg2 ? arg2 : "");
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
                    // condArg is not expanded
                    const char *branch = (condArg[0] == '\0') ? elseArg : thenArg;
                    const char *remaining = p;

                    expand_text_impl(table, branch, out_char, userdata);
                    // once done, continue expansion on the leftover
                    expand_text_impl(table, remaining, out_char, userdata);

                    free(condArg);
                    free(thenArg);
                    free(elseArg);
                    return; // we consumed remainder
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

                    expand_text_impl(table, includedText, out_char, userdata);
                    expand_text_impl(table, remaining, out_char, userdata);

                    free(includedText);
                    free(pathArg);
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

                    // 1) expand AFTER fully into a string
                    char *expandedAfter = expand_text_into_string(table, afterArg);
                    // 2) combine
                    char *combined = combine_strings(beforeArg, expandedAfter);

                    // 3) expand combined from the start
                    expand_text_impl(table, combined, out_char, userdata);
                    // 4) then expand leftover
                    expand_text_impl(table, remaining, out_char, userdata);

                    free(beforeArg);
                    free(afterArg);
                    free(expandedAfter);
                    free(combined);
                    return;
                }
                else {
                    // user-defined macro
                    char *macroVal = lookup_macro(table, nameBuf);
                    if (!macroVal) {
                        fprintf(stderr, "Error: Macro '%s' not defined\n", nameBuf);
                        exit(EXIT_FAILURE);
                    }
                    // read single argument
                    char *arg1 = readArg(&p);
                    if (!arg1) {
                        fprintf(stderr, "Error: Macro '%s' used without argument\n", nameBuf);
                        exit(EXIT_FAILURE);
                    }
                    free(nameBuf); // done with the macro name

                    // now expand the macro
                    const char *m = macroVal;
                    size_t outCap = 256;
                    size_t outLen = 0;
                    char *outBuf = malloc(outCap);
                    if (!outBuf) exit(EXIT_FAILURE);

                    while (*m) {
                        if (*m == '\\') {
                            // handle escapes inside macroVal
                            char next = *(m+1);
                            if (next == '\0') {
                                outBuf[outLen++] = '\\';
                                m++;
                            }
                            else if (next == '\\' || next == '{' || next == '}' ||
                                     next == '#' || next == '%') {
                                // remove the backslash, output the next
                                outBuf[outLen++] = next;
                                m += 2;
                            }
                            else if (isalnum((unsigned char)next)) {
                                // keep them literally, as the macroVal has a backslash + word
                                outBuf[outLen++] = '\\';
                                outBuf[outLen++] = next;
                                m += 2;
                            }
                            else {
                                // backslash + something else => just copy both
                                outBuf[outLen++] = '\\';
                                outBuf[outLen++] = next;
                                m += 2;
                            }
                        }
                        else if (*m == '#') {
                            // substitute the argument
                            size_t argLen = strlen(arg1);
                            while (outLen + argLen + 1 >= outCap) {
                                outCap *= 2;
                                outBuf = realloc(outBuf, outCap);
                                if (!outBuf) exit(EXIT_FAILURE);
                            }
                            memcpy(outBuf + outLen, arg1, argLen);
                            outLen += argLen;
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

                    const char *remaining = p;

                    // re-parse the expanded text 
                    expand_text_impl(table, outBuf, out_char, userdata);

                    // parse the leftover text
                    expand_text_impl(table, remaining, out_char, userdata);

                    free(outBuf);
                    free(arg1);
                    return; // end this function to avoid double-handling
                }

            } else {
                // backslash + something else => output both
                out_char('\\', userdata);
                if (*p) {
                    out_char(*p, userdata);
                    p++;
                }
            }
        } else {
            // Normal (non-backslash) character
            out_char(*p, userdata);
            p++;
        }
    }
}

/****************************************
 * parse_and_expand - public function
 ****************************************/

static void parse_and_expand(MacroTable *table, int argc, char *argv[]) {
    char *input = read_all_input_and_remove_comments(argc, argv);
    expand_text_impl(table, input, out_char_stdout, NULL);
    free(input);
}

/****************************************
 *                 main                 
 ****************************************/

int main(int argc, char *argv[]) {
    MacroTable table;
    init_macro_table(&table);

    parse_and_expand(&table, argc, argv);

    free_macro_table(&table);
    return 0;
}
