/**
 * @file repl.c
 * @brief Main REPL loop, state lifecycle, and history persistence.
 *
 * This is the thin orchestration layer. All heavy lifting is delegated to:
 *   repl_highlight.c  — syntax highlighting
 *   repl_readline.c   — line editing + completion
 *   repl_eval.c       — code synthesis + helpers
 *   repl_commands.c   — :command dispatch
 */

#include "repl_state.h"

/* ── State lifecycle ───────────────────────────────────────────────── */

void repl_state_init(ReplState *state, const char *self_path)
{
    memset(state, 0, sizeof(*state));
    state->self_path = self_path;
    state->history_cap = 64;
    state->history = xmalloc(state->history_cap * sizeof(char *));
    state->history_len = 0;
    state->watches_len = 0;
    state->symbol_count = 0;
    state->symbol_cap = 0;
    state->symbols = NULL;
    state->docs = NULL;
    state->doc_count = 0;

    for (int i = 0; i < REPL_MAX_WATCHES; i++)
    {
        state->watches[i] = NULL;
    }

    const char *home = getenv("HOME");
    if (z_is_windows() && !home)
    {
        home = getenv("USERPROFILE");
    }
    if (home)
    {
        snprintf(state->history_path, sizeof(state->history_path), "%s/.zprep_history", home);
    }
    else
    {
        state->history_path[0] = 0;
    }
}

void repl_state_free(ReplState *state)
{
    for (int i = 0; i < state->history_len; i++)
    {
        free(state->history[i]);
    }
    free(state->history);

    for (int i = 0; i < state->watches_len; i++)
    {
        free(state->watches[i]);
    }

    for (int i = 0; i < state->symbol_count; i++)
    {
        free(state->symbols[i]);
    }
    free(state->symbols);
}

void repl_history_add(ReplState *state, const char *line)
{
    if (state->history_len >= state->history_cap)
    {
        state->history_cap *= 2;
        state->history = realloc(state->history, state->history_cap * sizeof(char *));
    }
    state->history[state->history_len++] = strdup(line);
}

/* ── History persistence ───────────────────────────────────────────── */

static void repl_load_history(ReplState *state)
{
    if (!state->history_path[0])
    {
        return;
    }

    FILE *hf = fopen(state->history_path, "r");
    if (!hf)
    {
        return;
    }

    char buf[MAX_ERROR_MSG_LEN];
    while (fgets(buf, sizeof(buf), hf))
    {
        size_t l = strlen(buf);
        if (l > 0 && buf[l - 1] == '\n')
        {
            buf[--l] = 0;
        }
        if (l == 0)
        {
            continue;
        }
        repl_history_add(state, buf);
    }
    fclose(hf);

    if (state->history_len > 0)
    {
        printf("Loaded %d entries from history.\n", state->history_len);
    }
}

static void repl_load_init_script(ReplState *state)
{
    const char *home = getenv("HOME");
    if (z_is_windows() && !home)
    {
        home = getenv("USERPROFILE");
    }
    if (!home)
    {
        return;
    }

    char init_path[MAX_PATH_LEN];
    snprintf(init_path, sizeof(init_path), "%s/.zprep_init.zc", home);
    FILE *init_f = fopen(init_path, "r");
    if (!init_f)
    {
        return;
    }

    char buf[MAX_ERROR_MSG_LEN];
    int init_count = 0;
    while (fgets(buf, sizeof(buf), init_f))
    {
        size_t l = strlen(buf);
        if (l > 0 && buf[l - 1] == '\n')
        {
            buf[--l] = 0;
        }
        char *p = buf;
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }
        if (*p == 0 || *p == '/' || *p == '#')
        {
            continue;
        }
        repl_history_add(state, p);
        init_count++;
    }
    fclose(init_f);

    if (init_count > 0)
    {
        printf("Loaded %d lines from ~/.zprep_init.zc\n", init_count);
    }
}

void repl_save_history(ReplState *state)
{
    if (!state->history_path[0])
    {
        return;
    }

    FILE *hf = fopen(state->history_path, "w");
    if (!hf)
    {
        return;
    }

    int start = state->history_len > 1000 ? state->history_len - 1000 : 0;
    for (int i = start; i < state->history_len; i++)
    {
        fprintf(hf, "%s\n", state->history[i]);
    }
    fclose(hf);
}

/* ── Main loop ─────────────────────────────────────────────────────── */

void run_repl(const char *self_path)
{
    printf("\033[1;36mZen C REPL (%s)\033[0m\n", ZEN_VERSION);
    printf("Type 'exit' or 'quit' to leave.\n");
    printf("Type :help for commands.\n");

    ReplState state;
    repl_state_init(&state, self_path);
    repl_load_history(&state);
    repl_load_init_script(&state);

    char line_buf[MAX_ERROR_MSG_LEN];
    char *input_buffer = NULL;
    size_t input_len = 0;
    int brace_depth = 0;
    int paren_depth = 0;

    while (1)
    {
        /* Build prompt */
        char cwd[MAX_PATH_LEN];
        char prompt_text[MAX_PATH_LEN + 64];
        if (getcwd(cwd, sizeof(cwd)))
        {
            char *base = strrchr(cwd, '/');
            if (base)
            {
                base++;
            }
            else
            {
                base = cwd;
            }
            snprintf(prompt_text, sizeof(prompt_text), "\033[1;32m%s >>>\033[0m ", base);
        }
        else
        {
            snprintf(prompt_text, sizeof(prompt_text), "\033[1;32m>>>\033[0m ");
        }

        const char *prompt = (brace_depth > 0 || paren_depth > 0) ? "... " : prompt_text;
        int indent = (brace_depth > 0) ? brace_depth : 0;
        char *rline = repl_readline(&state, prompt, indent);

        if (!rline)
        {
            break;
        }
        strncpy(line_buf, rline, sizeof(line_buf) - 2);
        line_buf[sizeof(line_buf) - 2] = 0;
        strcat(line_buf, "\n");
        free(rline);

        if (NULL == input_buffer)
        {
            size_t cmd_len = strlen(line_buf);
            char cmd_buf[MAX_ERROR_MSG_LEN];
            snprintf(cmd_buf, sizeof(cmd_buf), "%s", line_buf);
            if (cmd_len > 0 && cmd_buf[cmd_len - 1] == '\n')
            {
                cmd_buf[--cmd_len] = 0;
            }
            while (cmd_len > 0 && (cmd_buf[cmd_len - 1] == ' ' || cmd_buf[cmd_len - 1] == '\t'))
            {
                cmd_buf[--cmd_len] = 0;
            }

            /* Exit commands */
            if (0 == strcmp(cmd_buf, "exit") || 0 == strcmp(cmd_buf, "quit"))
            {
                break;
            }

            /* Shell escape */
            if (cmd_buf[0] == '!')
            {
                int ret = system(cmd_buf + 1);
                printf("(exit code: %d)\n", ret);
                continue;
            }

            /* Command dispatch */
            if (cmd_buf[0] == ':')
            {
                int result = repl_dispatch_command(&state, cmd_buf);
                if (result == REPL_QUIT)
                {
                    break;
                }
                if (result == REPL_HANDLED)
                {
                    continue;
                }
                /* REPL_UNKNOWN falls through to eval */
            }
        }

        /* Track brace/paren depth for multi-line input */
        int in_quote = 0;
        int escaped = 0;
        for (int i = 0; line_buf[i]; i++)
        {
            char c = line_buf[i];
            if (escaped)
            {
                escaped = 0;
                continue;
            }
            if (c == '\\')
            {
                escaped = 1;
                continue;
            }
            if (c == '"')
            {
                in_quote = !in_quote;
                continue;
            }

            if (!in_quote)
            {
                if (c == '{')
                {
                    brace_depth++;
                }
                if (c == '}')
                {
                    brace_depth--;
                }
                if (c == '(')
                {
                    paren_depth++;
                }
                if (c == ')')
                {
                    paren_depth--;
                }
            }
        }

        size_t len = strlen(line_buf);
        input_buffer = realloc(input_buffer, input_len + len + 1);
        snprintf(input_buffer + input_len, input_len + sizeof(line_buf), "%s", line_buf);
        input_len += len;

        if (brace_depth > 0 || paren_depth > 0)
        {
            continue;
        }

        if (input_len > 0 && input_buffer[input_len - 1] == '\n')
        {
            input_buffer[--input_len] = 0;
        }

        if (input_len == 0)
        {
            free(input_buffer);
            input_buffer = NULL;
            input_len = 0;
            brace_depth = 0;
            paren_depth = 0;
            continue;
        }

        repl_history_add(&state, input_buffer);

        free(input_buffer);
        input_buffer = NULL;
        input_len = 0;
        brace_depth = 0;
        paren_depth = 0;

        /* Synthesize full program */
        char *global_code = NULL;
        char *main_code = NULL;
        repl_get_code(state.history, state.history_len, &global_code, &main_code);

        size_t total_size = strlen(global_code) + strlen(main_code) + 4096;
        if (state.watches_len > 0)
        {
            total_size += 16 * 1024;
        }

        char *full_code = malloc(total_size);
        snprintf(full_code, total_size, "%s\nfn main() { _z_suppress_stdout(); %s", global_code,
                 main_code);
        free(global_code);
        free(main_code);

        strcat(full_code, "_z_restore_stdout(); ");

        /* Auto-print detection for expressions */
        if (state.history_len > 0 && !is_header_line(state.history[state.history_len - 1]))
        {
            char *last_line = state.history[state.history_len - 1];

            char *check_buf = malloc(strlen(last_line) + 2);
            snprintf(check_buf, strlen(last_line) + 2, "%s", last_line);
            strcat(check_buf, ";");

            ParserContext ctx = {0};
            ctx.is_repl = 1;
            ctx.skip_preamble = 1;
            ctx.is_fault_tolerant = 1;
            ctx.on_error = repl_error_callback;
            Lexer l;
            lexer_init(&l, check_buf);
            ASTNode *node = parse_statement(&ctx, &l);
            free(check_buf);

            int is_expr = 0;
            if (node)
            {
                ASTNode *child = node;
                if (child->type == NODE_EXPR_BINARY || child->type == NODE_EXPR_UNARY ||
                    child->type == NODE_EXPR_LITERAL || child->type == NODE_EXPR_VAR ||
                    child->type == NODE_EXPR_CALL || child->type == NODE_EXPR_MEMBER ||
                    child->type == NODE_EXPR_INDEX || child->type == NODE_EXPR_CAST ||
                    child->type == NODE_EXPR_SIZEOF || child->type == NODE_EXPR_STRUCT_INIT ||
                    child->type == NODE_EXPR_ARRAY_LITERAL || child->type == NODE_EXPR_SLICE ||
                    child->type == NODE_TERNARY || child->type == NODE_MATCH)
                {
                    is_expr = 1;
                }
            }

            if (is_expr)
            {
                char *probe_global_code = NULL;
                char *probe_main_code = NULL;
                repl_get_code(state.history, state.history_len - 1, &probe_global_code,
                              &probe_main_code);

                size_t probesz =
                    strlen(probe_global_code) + strlen(probe_main_code) + strlen(last_line) + 4096;
                char *probe_code = malloc(probesz);

                snprintf(probe_code, probesz, "%s\nfn main() { _z_suppress_stdout(); %s",
                         probe_global_code, probe_main_code);
                free(probe_global_code);
                free(probe_main_code);

                strcat(probe_code, " raw { typedef struct { int _u; } __REVEAL_TYPE__; } ");
                strcat(probe_code, " var _z_type_probe: __REVEAL_TYPE__; _z_type_probe = (");
                strcat(probe_code, last_line);
                strcat(probe_code, "); }");

                char p_path[MAX_PATH_SIZE];
                snprintf(p_path, sizeof(p_path), "%s/zprep_repl_probe_%d.zc", z_get_temp_dir(),
                         rand());
                FILE *pf = fopen(p_path, "w");
                if (pf)
                {
                    fprintf(pf, "%s", probe_code);
                    fclose(pf);

                    char p_cmd[2048];
                    snprintf(p_cmd, sizeof(p_cmd), "%s run -q %s 2>&1", state.self_path, p_path);

                    FILE *pp = popen(p_cmd, "r");
                    int is_void = 0;
                    if (pp)
                    {
                        char buf[MAX_ERROR_MSG_LEN];
                        while (fgets(buf, sizeof(buf), pp))
                        {
                            if (strstr(buf, "void") && strstr(buf, "expression"))
                            {
                                is_void = 1;
                            }
                        }
                        pclose(pp);
                    }

                    if (!is_void)
                    {
                        strcat(full_code, "println \"{");
                        strcat(full_code, last_line);
                        strcat(full_code, "}\";");
                    }
                    else
                    {
                        strcat(full_code, last_line);
                    }
                }
                else
                {
                    strcat(full_code, last_line);
                }
                free(probe_code);
            }
            else
            {
                strcat(full_code, last_line);
            }
        }

        /* Append watch expressions */
        if (state.watches_len > 0)
        {
            strcat(full_code, "; ");
            for (int i = 0; i < state.watches_len; i++)
            {
                char wbuf[MAX_ERROR_MSG_LEN];
                snprintf(wbuf, sizeof(wbuf),
                         "printf(\"\\033[90mwatch:%s = \\033[0m\"); print \"{%s}\"; "
                         "printf(\"\\n\"); ",
                         state.watches[i], state.watches[i]);
                strcat(full_code, wbuf);
            }
        }

        strcat(full_code, " }");

        /* Write and execute */
        char tmp_path[MAX_PATH_SIZE];
        snprintf(tmp_path, sizeof(tmp_path), "%s/zprep_repl_%d.zc", z_get_temp_dir(), rand());
        FILE *f = fopen(tmp_path, "w");
        if (!f)
        {
            printf("Error: Cannot write temp file\n");
            free(full_code);
            break;
        }
        fprintf(f, "%s", full_code);
        fclose(f);
        free(full_code);

        char cmd[MAX_PATH_LEN];
        snprintf(cmd, sizeof(cmd), "%s run -q %s", state.self_path, tmp_path);

        int ret = system(cmd);
        printf("\n");

        if (0 != ret)
        {
            free(state.history[--state.history_len]);
        }
        else
        {
            /* Update session symbols for tab completion after successful eval */
            repl_update_symbols(&state);
        }

        /* Save history after each successful entry */
        repl_save_history(&state);
    }

    repl_save_history(&state);
    repl_state_free(&state);

    if (input_buffer)
    {
        free(input_buffer);
    }
}
