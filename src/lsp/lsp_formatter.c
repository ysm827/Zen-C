#include "../constants.h"
#include "lsp_formatter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char *lsp_format_source(const char *src)
{
    if (!src)
    {
        return NULL;
    }

    size_t len = strlen(src);
    char *out = malloc(len * 2 + 1); // Extra space for added characters
    char *dst = out;
    const char *s = src;

    int indent_level = 0;
    int at_start_of_line = 1;

    while (*s)
    {
        // Skip leading whitespace on new lines
        if (at_start_of_line)
        {
            while (*s == ' ' || *s == '\t')
            {
                s++;
            }
            if (!*s)
            {
                break;
            }

            // Adjust indent if next char is '}'
            int temp_indent = indent_level;
            if (*s == '}')
            {
                temp_indent--;
            }
            if (temp_indent < 0)
            {
                temp_indent = 0;
            }

            for (int i = 0; i < temp_indent * 4; i++)
            {
                *dst++ = ' ';
            }
            at_start_of_line = 0;
        }

        if (*s == '{')
        {
            *dst++ = '{';
            indent_level++;
            // If next is not newline, add newline? (Optional, let's keep it simple)
        }
        else if (*s == '}')
        {
            if (indent_level > 0 && !at_start_of_line)
            {
                // Actually, if we just reached '}' after some content on the same line,
                // we should have handled indent already.
            }
            if (*s == '}')
            {
                // We already handled indentation at start of line.
            }
            *dst++ = '}';
            if (indent_level > 0)
            {
                indent_level--;
            }
        }
        else if (*s == '\n')
        {
            *dst++ = '\n';
            at_start_of_line = 1;
        }
        else
        {
            *dst++ = *s;
        }
        s++;
    }

    *dst = 0;
    return out;
}
