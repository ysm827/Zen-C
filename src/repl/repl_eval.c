/**
 * @file repl_eval.c
 * @brief Evaluation helpers: code synthesis, header detection, documentation,
 *        session symbol extraction, and error callbacks.
 */

#include "repl_state.h"

/* ── Header-line detection ─────────────────────────────────────────── */

int is_header_line(const char *line)
{
    // Skip whitespace
    while (*line && (*line == ' ' || *line == '\t'))
    {
        line++;
    }
    if (strncmp(line, "struct", 6) == 0)
    {
        return 1;
    }
    if (strncmp(line, "impl", 4) == 0)
    {
        return 1;
    }
    if (strncmp(line, "fn", 2) == 0)
    {
        return 1;
    }
    if (strncmp(line, "use", 3) == 0)
    {
        return 1;
    }
    if (strncmp(line, "include", 7) == 0)
    {
        return 1;
    }
    if (strncmp(line, "typedef", 7) == 0)
    {
        return 1;
    }
    if (strncmp(line, "enum", 4) == 0)
    {
        return 1;
    }
    if (strncmp(line, "const", 5) == 0)
    {
        return 1;
    }
    if (strncmp(line, "def", 3) == 0)
    {
        return 1;
    }
    if (strncmp(line, "#include", 8) == 0)
    {
        return 1;
    }
    if (strncmp(line, "import", 6) == 0)
    {
        return 1;
    }

    return 0;
}

/* ── Error callback ────────────────────────────────────────────────── */

void repl_error_callback(void *data, Token t, const char *msg)
{
    (void)data;
    (void)t;
    fprintf(stderr, "\033[1;31merror:\033[0m %s\n", msg);
}

/* ── Definition lookup ─────────────────────────────────────────────── */

int is_definition_of(const char *code, const char *name)
{
    Lexer l;
    lexer_init(&l, code);
    Token t = lexer_next(&l);
    int is_header = 0;

    if (t.type == TOK_UNION)
    {
        is_header = 1;
    }
    else if (t.type == TOK_IDENT)
    {
        if ((t.len == 2 && strncmp(t.start, "fn", 2) == 0) ||
            (t.len == 6 && strncmp(t.start, "struct", 6) == 0) ||
            (t.len == 4 && strncmp(t.start, "enum", 4) == 0) ||
            (t.len == 7 && strncmp(t.start, "typedef", 7) == 0) ||
            (t.len == 5 && strncmp(t.start, "const", 5) == 0))
        {
            is_header = 1;
        }
    }

    if (is_header)
    {
        Token name_tok = lexer_next(&l);
        if (name_tok.type == TOK_IDENT)
        {
            if (strlen(name) == (size_t)name_tok.len &&
                strncmp(name, name_tok.start, name_tok.len) == 0)
            {
                return 1;
            }
        }
    }
    return 0;
}

/* ── Command prefix check ─────────────────────────────────────────── */

int is_command(const char *buf, const char *cmd)
{
    if (buf[0] != ':')
    {
        return 0;
    }
    size_t cmd_len = strlen(cmd);
    if (strncmp(buf + 1, cmd, cmd_len) != 0)
    {
        return 0;
    }
    char next = buf[1 + cmd_len];
    return next == 0 || isspace(next);
}

/* ── Code synthesis ────────────────────────────────────────────────── */

void repl_get_code(char **history, int len, char **out_global, char **out_main)
{
    size_t total_len = 0;
    for (int i = 0; i < len; i++)
    {
        total_len += strlen(history[i]) + 2;
    }

    char *global_buf = malloc(total_len + 1);
    char *main_buf = malloc(total_len + 1);
    global_buf[0] = 0;
    main_buf[0] = 0;

    int brace_depth = 0;
    int in_global = 0;

    for (int i = 0; i < len; i++)
    {
        char *line = history[i];

        if (brace_depth == 0)
        {
            if (is_header_line(line))
            {
                in_global = 1;
            }
            else
            {
                in_global = 0;
            }
        }

        if (in_global)
        {
            strcat(global_buf, line);
            strcat(global_buf, "\n");
        }
        else
        {
            strcat(main_buf, line);
            strcat(main_buf, " ");
        }

        for (char *p = line; *p; p++)
        {
            if (*p == '{')
            {
                brace_depth++;
            }
            else if (*p == '}')
            {
                brace_depth--;
            }
        }
    }

    *out_global = global_buf;
    *out_main = main_buf;
}

/* ── Documentation database ────────────────────────────────────────── */

void repl_load_docs(ReplState *state)
{
    if (state->docs)
    {
        return; /* Already loaded. */
    }

    const char *search_paths[] = {"src/repl/docs.json", // Dev path
                                  "docs.json",          // CWD
#ifdef ZEN_SHARE_DIR
                                  ZEN_SHARE_DIR "/docs.json", // Install path
#endif
                                  "/usr/local/share/zenc/docs.json",
                                  "/usr/share/zenc/docs.json",
                                  NULL};

    FILE *f = NULL;
    for (int i = 0; search_paths[i]; i++)
    {
        f = fopen(search_paths[i], "r");
        if (f)
        {
            break;
        }
    }

    if (!f)
    {
        return;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc(len + 1);
    if (data)
    {
        fread(data, 1, len, f);
        data[len] = 0;
    }
    fclose(f);

    if (!data)
    {
        return;
    }

    cJSON *json = cJSON_Parse(data);
    free(data);

    if (!json)
    {
        return;
    }

    if (cJSON_IsArray(json))
    {
        state->doc_count = cJSON_GetArraySize(json);
        state->docs = calloc(state->doc_count + 1, sizeof(ReplDoc));

        cJSON *item = NULL;
        int i = 0;
        cJSON_ArrayForEach(item, json)
        {
            cJSON *name = cJSON_GetObjectItem(item, "name");
            cJSON *doc = cJSON_GetObjectItem(item, "doc");

            if (cJSON_IsString(name))
            {
                state->docs[i].name = strdup(name->valuestring);
            }
            if (cJSON_IsString(doc))
            {
                state->docs[i].doc = strdup(doc->valuestring);
            }

            i++;
        }
    }
    cJSON_Delete(json);
}

const ReplDoc *repl_find_doc(ReplState *state, const char *name)
{
    repl_load_docs(state);

    if (!state->docs)
    {
        return NULL;
    }

    for (int i = 0; i < state->doc_count; i++)
    {
        if (state->docs[i].name && strcmp(name, state->docs[i].name) == 0)
        {
            return &state->docs[i];
        }
    }
    return NULL;
}

/* ── Session symbol extraction (for tab completion) ────────────────── */

static void repl_add_symbol(ReplState *state, const char *name)
{
    if (!name || !name[0])
    {
        return;
    }

    /* Deduplicate */
    for (int i = 0; i < state->symbol_count; i++)
    {
        if (strcmp(state->symbols[i], name) == 0)
        {
            return;
        }
    }

    if (state->symbol_count >= state->symbol_cap)
    {
        state->symbol_cap = state->symbol_cap ? state->symbol_cap * 2 : 64;
        state->symbols = realloc(state->symbols, state->symbol_cap * sizeof(char *));
    }

    state->symbols[state->symbol_count++] = strdup(name);
}

void repl_update_symbols(ReplState *state)
{
    /* Free old symbols */
    for (int i = 0; i < state->symbol_count; i++)
    {
        free(state->symbols[i]);
    }
    state->symbol_count = 0;

    /* Add stdlib type names */
    static const char *STDLIB_TYPES[] = {
        "Vec",    "String", "Map",    "Set",    "Stack",  "Queue",  "Option",
        "Result", "Arena",  "Slice",  "Regex",  "BigInt", "BigFloat", "Complex",
        "File",   "Path",   "Thread", "Mutex",  NULL};

    for (int i = 0; STDLIB_TYPES[i]; i++)
    {
        repl_add_symbol(state, STDLIB_TYPES[i]);
    }

    if (state->history_len == 0)
    {
        return;
    }

    /* Parse the session to extract user symbols */
    char *global_code = NULL;
    char *main_code = NULL;
    repl_get_code(state->history, state->history_len, &global_code, &main_code);

    size_t code_size = strlen(global_code) + strlen(main_code) + 128;
    char *code = malloc(code_size);
    snprintf(code, code_size, "%s\nfn main() { %s }", global_code, main_code);
    free(global_code);
    free(main_code);

    ParserContext ctx = {0};
    ctx.is_repl = 1;
    ctx.skip_preamble = 1;
    ctx.is_fault_tolerant = 1;
    ctx.on_error = repl_error_callback;

    Lexer l;
    lexer_init(&l, code);
    ASTNode *nodes = parse_program(&ctx, &l);

    ASTNode *search = nodes;
    if (search && search->type == NODE_ROOT)
    {
        search = search->root.children;
    }

    for (ASTNode *n = search; n; n = n->next)
    {
        if (n->type == NODE_FUNCTION)
        {
            repl_add_symbol(state, n->func.name);
        }
        else if (n->type == NODE_STRUCT)
        {
            repl_add_symbol(state, n->strct.name);
        }
    }

    /* Extract variables from main's body */
    for (ASTNode *n = search; n; n = n->next)
    {
        if (n->type == NODE_FUNCTION && strcmp(n->func.name, "main") == 0)
        {
            if (n->func.body && n->func.body->type == NODE_BLOCK)
            {
                for (ASTNode *s = n->func.body->block.statements; s; s = s->next)
                {
                    if (s->type == NODE_VAR_DECL)
                    {
                        repl_add_symbol(state, s->var_decl.name);
                    }
                }
            }
            break;
        }
    }

    free(code);
}
