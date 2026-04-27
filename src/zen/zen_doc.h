#ifndef ZEN_DOC_H
#define ZEN_DOC_H

#include "../ast/ast.h"
#include "../zprep.h"

/**
 * @brief Generates documentation for the given program AST.
 *
 * @param ctx The parser context (useful for type resolution).
 * @param root The root NODE_ROOT of the AST.
 */
void generate_docs(struct ParserContext *ctx, ASTNode *root);

#endif
