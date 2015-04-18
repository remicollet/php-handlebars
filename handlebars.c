
/* vim: tabstop=4:softtabstop=4:shiftwidth=4:expandtab */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <talloc.h>

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "zend_types.h"

#ifdef ZTS
#include "TSRM.h"
#endif

#include "php_handlebars.h"
//#include "compat.h"

#include "handlebars.h"
#include "handlebars_ast.h"
#include "handlebars_ast_list.h"
#include "handlebars_ast_printer.h"
#include "handlebars_compiler.h"
#include "handlebars_context.h"
#include "handlebars_memory.h"
#include "handlebars_opcode_printer.h"
#include "handlebars_opcodes.h"
#include "handlebars_token.h"
#include "handlebars_token_list.h"
#include "handlebars_token_printer.h"
#include "handlebars.tab.h"
#include "handlebars.lex.h"

ZEND_DECLARE_MODULE_GLOBALS(handlebars)

static void php_handlebars_ast_node_to_zval(struct handlebars_ast_node * node, zval * current TSRMLS_DC);
static void php_handlebars_compiler_to_zval(struct handlebars_compiler * compiler, zval * current TSRMLS_DC);
static void php_handlebars_ast_list_to_zval(struct handlebars_ast_list * list, zval * current TSRMLS_DC);

/* {{{ PHP7 Compat ---------------------------------------------------------- */

#if PHP_MAJOR_VERSION < 7
#define _add_assoc_string(...) add_assoc_string(__VA_ARGS__, 1)
#define _add_assoc_string_ex(...) add_assoc_string_ex(__VA_ARGS__, 1)
#define _add_assoc_stringl_ex(...) add_assoc_stringl_ex(__VA_ARGS__, 1)
#define _RETURN_STRING(a) RETURN_STRING(a, 1)
#define _RETVAL_STRING(a) RETVAL_STRING(a, 1)
#define _DECLARE_ZVAL(name) zval * name
#define _ALLOC_INIT_ZVAL(name) ALLOC_INIT_ZVAL(name)
typedef int strsize_t;
#else
#define _add_assoc_string(z, k, s) add_assoc_string_ex(z, k, strlen(k)+1, s)
#define _add_assoc_string_ex add_assoc_string_ex
#define _add_assoc_stringl_ex add_assoc_stringl_ex
#define _RETURN_STRING(a) RETURN_STRING(a)
#define _RETVAL_STRING(a) RETVAL_STRING(a)
#define _DECLARE_ZVAL(name) zval name ## _v; zval * name = &name ## _v
#define _ALLOC_INIT_ZVAL(name) ZVAL_NULL(name)
typedef size_t strsize_t;
#endif

#define _DECLARE_ALLOC_INIT_ZVAL(name) _DECLARE_ZVAL(name); _ALLOC_INIT_ZVAL(name)

/* }}} ---------------------------------------------------------------------- */
/* {{{ Utils ---------------------------------------------------------------- */

#define add_assoc_handlebars_ast_node_ex(current, str, node) \
    add_assoc_handlebars_ast_node(current, ZEND_STRS(str), node TSRMLS_CC)

#define add_assoc_handlebars_ast_list_ex(current, str, list) \
    add_assoc_handlebars_ast_list(current, ZEND_STRS(str), list TSRMLS_CC)

#define add_next_index_handlebars_ast_node_ex(current, node) \
    add_next_index_handlebars_ast_node(current, node TSRMLS_CC)

static inline void add_assoc_handlebars_ast_node(zval * current, const char * key, size_t length, 
        struct handlebars_ast_node * node TSRMLS_DC)
{
    _DECLARE_ZVAL(tmp);

    if( node ) {
        _ALLOC_INIT_ZVAL(tmp);
        php_handlebars_ast_node_to_zval(node, tmp TSRMLS_CC);
        add_assoc_zval_ex(current, key, length, tmp);
    }
}

static inline void add_assoc_handlebars_ast_list(zval * current, const char * key, size_t length, 
        struct handlebars_ast_list * list TSRMLS_DC)
{
    _DECLARE_ZVAL(tmp);
    
    if( list ) {
        _ALLOC_INIT_ZVAL(tmp);
        php_handlebars_ast_list_to_zval(list, tmp TSRMLS_CC);
        add_assoc_zval_ex(current, key, length, tmp);
    }
}

static inline void add_next_index_handlebars_ast_node(zval * current, struct handlebars_ast_node * node TSRMLS_DC)
{
    _DECLARE_ZVAL(tmp);

    if( node ) {
        _ALLOC_INIT_ZVAL(tmp);
        php_handlebars_ast_node_to_zval(node, tmp TSRMLS_CC);
        add_next_index_zval(current, tmp);
    }
}

static void php_handlebars_error(char * msg TSRMLS_DC)
{
    if( HANDLEBARS_G(handlebars_last_error) ) {
        efree(HANDLEBARS_G(handlebars_last_error));
        HANDLEBARS_G(handlebars_last_error) = NULL;
    }
    if( msg ) {
        HANDLEBARS_G(handlebars_last_error) = estrdup(msg);
    } else {
        HANDLEBARS_G(handlebars_last_error) = NULL;
    }
}

/* }}} ---------------------------------------------------------------------- */
/* {{{ Data Conversion (inline) --------------------------------------------- */

static inline void php_handlebars_ast_list_to_zval(struct handlebars_ast_list * list, zval * current TSRMLS_DC)
{
    struct handlebars_ast_list_item * item;
    struct handlebars_ast_list_item * ltmp;
    _DECLARE_ZVAL(tmp);
    
    if( list != NULL ) {
        array_init(current);
        
        handlebars_ast_list_foreach(list, item, ltmp) {
            add_next_index_handlebars_ast_node_ex(current, item->data);
        }
    }
}

static inline void php_handlebars_depths_to_zval(long depths, zval * current)
{
    int depthi = 1;

    array_init(current);
    
    while( depths > 0 ) {
        if( depths & 1 ) {
            add_next_index_long(current, depthi);
        }
        depthi++;
        depths = depths >> 1;
    }
}


static inline void php_handlebars_strip_to_zval(unsigned strip, zval * current)
{
    array_init(current);
    add_assoc_bool_ex(current, ZEND_STRS("left"), 1 && (strip & handlebars_ast_strip_flag_left));
    add_assoc_bool_ex(current, ZEND_STRS("right"), 1 && (strip & handlebars_ast_strip_flag_right));
    add_assoc_bool_ex(current, ZEND_STRS("openStandalone"), 1 && (strip & handlebars_ast_strip_flag_open_standalone));
    add_assoc_bool_ex(current, ZEND_STRS("closeStandalone"), 1 && (strip & handlebars_ast_strip_flag_close_standalone));
    add_assoc_bool_ex(current, ZEND_STRS("inlineStandalone"), 1 && (strip & handlebars_ast_strip_flag_inline_standalone));
    add_assoc_bool_ex(current, ZEND_STRS("leftStripped"), 1 && (strip & handlebars_ast_strip_flag_left_stripped));
    add_assoc_bool_ex(current, ZEND_STRS("rightStriped"), 1 && (strip & handlebars_ast_strip_flag_right_stripped));
}

/* }}} ---------------------------------------------------------------------- */
/* {{{ Data Conversion ------------------------------------------------------ */

static void php_handlebars_ast_node_to_zval(struct handlebars_ast_node * node, zval * current TSRMLS_DC)
{
    _DECLARE_ZVAL(tmp);
    array_init(current);
    
    if( node == NULL ) {
        return;
    }
    
    _add_assoc_string_ex(current, ZEND_STRS("type"), (char *) handlebars_ast_node_readable_type(node->type));
    
    if( node->strip ) {
        _ALLOC_INIT_ZVAL(tmp);
        php_handlebars_strip_to_zval(node->strip, tmp);
        add_assoc_zval_ex(current, ZEND_STRS("strip"), tmp);   
    }
    
    switch( node->type ) {
        case HANDLEBARS_AST_NODE_PROGRAM: {
            add_assoc_handlebars_ast_list_ex(current, "statements", node->node.program.statements);
            break;
        }
        case HANDLEBARS_AST_NODE_MUSTACHE: {
            add_assoc_handlebars_ast_node_ex(current, "sexpr", node->node.mustache.sexpr);
            add_assoc_long_ex(current, ZEND_STRS("unescaped"), node->node.mustache.unescaped);
            break;
        }
        case HANDLEBARS_AST_NODE_SEXPR: {
            add_assoc_handlebars_ast_node_ex(current, "hash", node->node.sexpr.hash);
            add_assoc_handlebars_ast_node_ex(current, "id", node->node.sexpr.id);
            add_assoc_handlebars_ast_list_ex(current, "params", node->node.sexpr.params);
            break;
        }
        case HANDLEBARS_AST_NODE_PARTIAL:
            add_assoc_handlebars_ast_node_ex(current, "partial_name", node->node.partial.partial_name);
            add_assoc_handlebars_ast_node_ex(current, "context", node->node.partial.context);
            add_assoc_handlebars_ast_node_ex(current, "hash", node->node.partial.hash);
            break;
        case HANDLEBARS_AST_NODE_RAW_BLOCK: {
            add_assoc_handlebars_ast_node_ex(current, "mustache", node->node.raw_block.mustache);
            add_assoc_handlebars_ast_node_ex(current, "program", node->node.raw_block.program);
            if( node->node.raw_block.close ) {
                _add_assoc_string_ex(current, ZEND_STRS("close"), node->node.raw_block.close);
            }
            break;
        }
        case HANDLEBARS_AST_NODE_BLOCK: {
            add_assoc_handlebars_ast_node_ex(current, "mustache", node->node.block.mustache);
            add_assoc_handlebars_ast_node_ex(current, "program", node->node.block.program);
            add_assoc_handlebars_ast_node_ex(current, "inverse", node->node.block.inverse);
            add_assoc_handlebars_ast_node_ex(current, "close", node->node.block.close);
            add_assoc_long_ex(current, ZEND_STRS("inverted"), node->node.block.inverted);
            break;
        }
        case HANDLEBARS_AST_NODE_CONTENT: {
            if( node->node.content.string ) {
                _add_assoc_stringl_ex(current, ZEND_STRS("string"),
                    node->node.content.string,
                    node->node.content.length);
            }
            if( node->node.content.original ) {
                _add_assoc_stringl_ex(current, ZEND_STRS("original"),
                    node->node.content.original,
                    strlen(node->node.content.original));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_HASH: {
            add_assoc_handlebars_ast_list_ex(current, "segments", node->node.hash.segments);
            break;
        }
        case HANDLEBARS_AST_NODE_HASH_SEGMENT: {
            if( node->node.hash_segment.key ) {
                _add_assoc_stringl_ex(current, ZEND_STRS("key"),
                    node->node.hash_segment.key,
                    node->node.hash_segment.key_length);
            }
            add_assoc_handlebars_ast_node_ex(current, "value", node->node.hash_segment.value);
            break;
        }
        case HANDLEBARS_AST_NODE_ID: {
            add_assoc_handlebars_ast_list_ex(current, "parts", node->node.id.parts);
            add_assoc_long_ex(current, ZEND_STRS("depth"), node->node.id.depth);
            add_assoc_long_ex(current, ZEND_STRS("is_simple"), node->node.id.is_simple);
            add_assoc_long_ex(current, ZEND_STRS("is_scoped"), node->node.id.is_scoped);
            if( node->node.id.id_name ) {
                _add_assoc_stringl_ex(current, ZEND_STRS("id_name"),
                    node->node.id.id_name,
                    node->node.id.id_name_length);
            }
            if( node->node.id.string ) {
                _add_assoc_stringl_ex(current, ZEND_STRS("string"),
                    node->node.id.string,
                    node->node.id.string_length);
            }
            if( node->node.id.original ) {
                _add_assoc_stringl_ex(current, ZEND_STRS("original"),
                    node->node.id.original,
                    node->node.id.original_length);
            }
            break;
        }
        case HANDLEBARS_AST_NODE_PARTIAL_NAME: {
            add_assoc_handlebars_ast_node_ex(current, "name", node->node.partial_name.name);
            break;
        }
        case HANDLEBARS_AST_NODE_DATA: {
            add_assoc_handlebars_ast_node_ex(current, "id", node->node.data.id);
            break;
        }
        case HANDLEBARS_AST_NODE_STRING: {
            if( node->node.string.string ) {
                _add_assoc_stringl_ex(current, ZEND_STRS("string"),
                    node->node.string.string,
                    node->node.string.length);
            }
            break;
        }
        case HANDLEBARS_AST_NODE_NUMBER: {
            if( node->node.number.string ) {
                _add_assoc_stringl_ex(current, ZEND_STRS("number"),
                    node->node.number.string,
                    node->node.number.length);
            }
            break;
        }
        case HANDLEBARS_AST_NODE_BOOLEAN: {
            if( node->node.boolean.string ) {
                _add_assoc_stringl_ex(current, ZEND_STRS("boolean"),
                    node->node.boolean.string,
                    node->node.boolean.length);
            }
            break;
        }
        case HANDLEBARS_AST_NODE_COMMENT: {
            if( node->node.comment.comment ) {
                _add_assoc_stringl_ex(current, ZEND_STRS("comment"),
                    node->node.comment.comment,
                    node->node.boolean.length);
            }
            break;
        }
        case HANDLEBARS_AST_NODE_PATH_SEGMENT: {
            if( node->node.path_segment.separator ) {
                _add_assoc_stringl_ex(current, ZEND_STRS("separator"),
                    node->node.path_segment.separator,
                    node->node.path_segment.separator_length);
            }
            if( node->node.path_segment.part ) {
                _add_assoc_stringl_ex(current, ZEND_STRS("part"),
                    node->node.path_segment.part,
                    node->node.path_segment.part_length);
            }
            break;
        }
        case HANDLEBARS_AST_NODE_NIL:
            break;
    }
}

static void php_handlebars_operand_append_zval(struct handlebars_operand * operand, zval * arr TSRMLS_DC)
{
    switch( operand->type ) {
        case handlebars_operand_type_null:
            add_next_index_null(arr);
            break;
        case handlebars_operand_type_boolean:
            add_next_index_bool(arr, (int) operand->data.boolval);
            break;
        case handlebars_operand_type_long:
            add_next_index_long(arr, operand->data.longval);
            break;
        case handlebars_operand_type_string:
            add_next_index_string(arr, operand->data.stringval, 1);
            break;
        case handlebars_operand_type_array: {
            _DECLARE_ZVAL(current);
            char ** tmp = operand->data.arrayval;
            
            _ALLOC_INIT_ZVAL(current);
            array_init(current);
            
            for( ; *tmp; ++tmp ) {
                add_next_index_string(current, *tmp, 1);
            }
            
            add_next_index_zval(arr, current);
            break;
        }
    }
}

static void php_handlebars_opcode_to_zval(struct handlebars_opcode * opcode, zval * current TSRMLS_DC)
{
    _DECLARE_ZVAL(args);
    short num = handlebars_opcode_num_operands(opcode->type);
    
    array_init(current);

    _add_assoc_string_ex(current, ZEND_STRS("opcode"), handlebars_opcode_readable_type(opcode->type));
    
    _ALLOC_INIT_ZVAL(args);
    array_init(args);
    if( num >= 1 ) {
        php_handlebars_operand_append_zval(&opcode->op1, args TSRMLS_CC);
    }
    if( num >= 2 ) {
        php_handlebars_operand_append_zval(&opcode->op2, args TSRMLS_CC);
    }
    if( num >= 3 ) {
        php_handlebars_operand_append_zval(&opcode->op3, args TSRMLS_CC);
    }
    add_assoc_zval_ex(current, "args", sizeof("args"), args);
}

static inline void php_handlebars_opcodes_to_zval(struct handlebars_opcode ** opcodes, size_t count, zval * current TSRMLS_DC)
{
    size_t i;
    struct handlebars_opcode ** pos = opcodes;
    short num;
    _DECLARE_ZVAL(tmp);
    
    array_init(current);
    
    for( i = 0; i < count; i++, pos++ ) {
        _ALLOC_INIT_ZVAL(tmp);
        php_handlebars_opcode_to_zval(*pos, tmp TSRMLS_CC);
        add_next_index_zval(current, tmp);
    }
}

static inline void php_handlebars_compilers_to_zval(struct handlebars_compiler ** compilers, size_t count, zval * current TSRMLS_DC)
{
    size_t i;
    struct handlebars_compiler * child;
    _DECLARE_ZVAL(tmp);

    array_init(current);

    for( i = 0; i < count; i++ ) {
        child = *(compilers + i);
        _ALLOC_INIT_ZVAL(tmp);
        php_handlebars_compiler_to_zval(child, tmp TSRMLS_CC);
        add_next_index_zval(current, tmp);
    }
}

static void php_handlebars_compiler_to_zval(struct handlebars_compiler * compiler, zval * current TSRMLS_DC)
{
    _DECLARE_ZVAL(tmp);
    
    array_init(current);
    
    // Opcodes
    _ALLOC_INIT_ZVAL(tmp);
    php_handlebars_opcodes_to_zval(compiler->opcodes, compiler->opcodes_length, tmp TSRMLS_CC);
    add_assoc_zval_ex(current, "opcodes", sizeof("opcodes"), tmp);
    
    // Children
    _ALLOC_INIT_ZVAL(tmp);
    php_handlebars_compilers_to_zval(compiler->children, compiler->children_length, tmp TSRMLS_CC);
    add_assoc_zval_ex(current, "children", sizeof("children"), tmp);
    
    // Depths
    _ALLOC_INIT_ZVAL(tmp);
    php_handlebars_depths_to_zval(compiler->depths, tmp);
    add_assoc_zval_ex(current, "depths", sizeof("depths"), tmp);
}

static char ** php_handlebars_compiler_known_helpers_from_zval(void * ctx, zval * arr TSRMLS_DC)
{
    HashTable * data_hash = NULL;
    HashPosition data_pointer = NULL;
    zval ** data_entry = NULL;
    long count = 0;
    char ** ptr;
    const char ** ptr2;
    char ** known_helpers;
    
    if( !arr || Z_TYPE_P(arr) != IS_ARRAY ) {
        return NULL;
    }
    
    data_hash = HASH_OF(arr);
    count = zend_hash_num_elements(data_hash);
    
    if( !count ) {
        return NULL;
    }

    // Count builtins >.>
    for( ptr2 = handlebars_builtins; *ptr2; ++ptr2, ++count );
    
    // Allocate array
    ptr = known_helpers = talloc_array(ctx, char *, count + 1);
        
    // Copy in known helpers
    zend_hash_internal_pointer_reset_ex(data_hash, &data_pointer);
    while( zend_hash_get_current_data_ex(data_hash, (void**) &data_entry, &data_pointer) == SUCCESS ) {
        if( Z_TYPE_PP(data_entry) == IS_STRING ) {
            *ptr++ = (char *) handlebars_talloc_strdup(ctx, Z_STRVAL_PP(data_entry));
        }
        zend_hash_move_forward_ex(data_hash, &data_pointer);
    }

    // Copy in builtins
    for( ptr2 = handlebars_builtins; *ptr2; ++ptr2 ) {
        *ptr++ = (char *) handlebars_talloc_strdup(ctx, *ptr2);
    }
    
    // Null terminate
    *ptr++ = NULL;

    return known_helpers;
}

/* }}} ---------------------------------------------------------------------- */
/* {{{ Functions ------------------------------------------------------------ */

PHP_FUNCTION(handlebars_error)
{
    if( HANDLEBARS_G(handlebars_last_error) ) {
        _RETURN_STRING(HANDLEBARS_G(handlebars_last_error));
    }
}

PHP_FUNCTION(handlebars_lex)
{
    char * tmpl;
    strsize_t tmpl_len;
    struct handlebars_context * ctx;
    struct handlebars_token_list * list;
    struct handlebars_token_list_item * el = NULL;
    struct handlebars_token_list_item * tmp = NULL;
    struct handlebars_token * token = NULL;
    _DECLARE_ZVAL(child);
    
    // Arguments
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &tmpl, &tmpl_len) == FAILURE ) {
        RETURN_FALSE;
    }
    
    ctx = handlebars_context_ctor();
    ctx->tmpl = tmpl;
    list = handlebars_lex(ctx);
    
    array_init(return_value);
    
    handlebars_token_list_foreach(list, el, tmp) {
        token = el->data;
        
        _ALLOC_INIT_ZVAL(child);
        array_init(child);
        _add_assoc_string_ex(child, ZEND_STRS("name"), (char *) handlebars_token_readable_type(token->token));
        if( token->text ) {
            _add_assoc_string_ex(child, ZEND_STRS("text"), token->text);
        }
        add_next_index_zval(return_value, child);
    }
    
    handlebars_context_dtor(ctx);
}

PHP_FUNCTION(handlebars_lex_print)
{
    char * tmpl;
    strsize_t tmpl_len;
    struct handlebars_context * ctx;
    struct handlebars_token_list * list;
    char * output;
    
    // Arguments
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &tmpl, &tmpl_len) == FAILURE ) {
        RETURN_FALSE;
    }
    
    ctx = handlebars_context_ctor();
    ctx->tmpl = tmpl;
    list = handlebars_lex(ctx);
    output = handlebars_token_list_print(list, 0);
    
    _RETVAL_STRING(output);
    
    handlebars_context_dtor(ctx);
}

PHP_FUNCTION(handlebars_parse)
{
    char * tmpl;
    strsize_t tmpl_len;
    struct handlebars_context * ctx;
    int retval;
    zval * output;
    char * errmsg;
    
    // Arguments
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &tmpl, &tmpl_len) == FAILURE ) {
        RETURN_FALSE;
    }
    
    ctx = handlebars_context_ctor();
    ctx->tmpl = tmpl;
    retval = handlebars_yy_parse(ctx);
    
    if( ctx->error != NULL ) {
        // errmsg will be freed by the destruction of ctx
        errmsg = handlebars_context_get_errmsg(ctx);
        php_handlebars_error(errmsg TSRMLS_CC);
        RETVAL_FALSE;
    } else {
        php_handlebars_ast_node_to_zval(ctx->program, return_value TSRMLS_CC);
    }
    
    handlebars_context_dtor(ctx);
}

PHP_FUNCTION(handlebars_parse_print)
{
    char * tmpl;
    strsize_t tmpl_len;
    struct handlebars_context * ctx;
    int retval;
    char * output;
    char * errmsg;
    
    // Arguments
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &tmpl, &tmpl_len) == FAILURE ) {
        RETURN_FALSE;
    }
    
    ctx = handlebars_context_ctor();
    ctx->tmpl = tmpl;
    retval = handlebars_yy_parse(ctx);
    
    if( ctx->error != NULL ) {
        // errmsg will be freed by the destruction of ctx
        errmsg = handlebars_context_get_errmsg(ctx);
        php_handlebars_error(errmsg TSRMLS_CC);
        RETVAL_FALSE;
    } else {
        output = handlebars_ast_print(ctx->program, 0);
        _RETVAL_STRING(output);
    }
    
    handlebars_context_dtor(ctx);
}

PHP_FUNCTION(handlebars_compile)
{
    char * tmpl;
    strsize_t tmpl_len;
    long flags = 0;
    zval * known_helpers = NULL;
    struct handlebars_context * ctx;
    struct handlebars_compiler * compiler;
    struct handlebars_opcode_printer * printer;
    int retval;
    zval * output;
    char * errmsg;
    char ** known_helpers_arr;
    
    // Arguments
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|lz", &tmpl, &tmpl_len, &flags, &known_helpers) == FAILURE ) {
        RETURN_FALSE;
    }
    
    // Initialize
    ctx = handlebars_context_ctor();
    compiler = handlebars_compiler_ctor(ctx);
    printer = handlebars_opcode_printer_ctor(ctx);
    handlebars_compiler_set_flags(compiler, flags);
    
    // Get known helpers
    known_helpers_arr = php_handlebars_compiler_known_helpers_from_zval(compiler, known_helpers TSRMLS_CC);
    if( known_helpers_arr ) {
        compiler->known_helpers = (const char **) known_helpers_arr;
    }
    
    // Parse
    ctx->tmpl = tmpl;
    retval = handlebars_yy_parse(ctx);
    
    if( ctx->error != NULL ) {
        // errmsg will be freed by the destruction of ctx
        errmsg = handlebars_context_get_errmsg(ctx);
        php_handlebars_error(errmsg TSRMLS_CC);
        RETVAL_FALSE;
        goto error;
    }
    
    // Compile
    handlebars_compiler_compile(compiler, ctx->program);
    if( compiler->errnum ) {
        // @todo decent error message
        RETVAL_FALSE;
        goto error;
    }
    
    php_handlebars_compiler_to_zval(compiler, return_value TSRMLS_CC);
    
error:
    handlebars_context_dtor(ctx);
}

PHP_FUNCTION(handlebars_compile_print)
{
    char * tmpl;
    strsize_t tmpl_len;
    long flags = 0;
    zval * known_helpers = NULL;
    struct handlebars_context * ctx;
    struct handlebars_compiler * compiler;
    struct handlebars_opcode_printer * printer;
    int retval;
    char * output;
    char * errmsg;
    char ** known_helpers_arr;
    
    // Arguments
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|lz", &tmpl, &tmpl_len, &flags, &known_helpers) == FAILURE ) {
        RETURN_FALSE;
    }
    
    // Initialize
    ctx = handlebars_context_ctor();
    compiler = handlebars_compiler_ctor(ctx);
    printer = handlebars_opcode_printer_ctor(ctx);
    handlebars_compiler_set_flags(compiler, flags);
    
    // Get known helpers
    known_helpers_arr = php_handlebars_compiler_known_helpers_from_zval(ctx, known_helpers TSRMLS_CC);
    if( known_helpers_arr ) {
        compiler->known_helpers = (const char **) known_helpers_arr;
    }
    
    // Parse
    ctx->tmpl = tmpl;
    retval = handlebars_yy_parse(ctx);
    
    if( ctx->error != NULL ) {
        // errmsg will be freed by the destruction of ctx
        errmsg = handlebars_context_get_errmsg(ctx);
        php_handlebars_error(errmsg TSRMLS_CC);
        RETVAL_FALSE;
        goto error;
    }
    
    // Compile
    handlebars_compiler_compile(compiler, ctx->program);
    if( compiler->errnum ) {
        // @todo decent error message
        RETVAL_FALSE;
        goto error;
    }
    
    // Printer
    handlebars_opcode_printer_print(printer, compiler);
    _RETVAL_STRING(printer->output);
    
error:
    handlebars_context_dtor(ctx);
}


PHP_FUNCTION(handlebars_version)
{
    _RETURN_STRING(handlebars_version_string());
}

/* }}} ---------------------------------------------------------------------- */
/* {{{ Module Hooks --------------------------------------------------------- */

static PHP_GINIT_FUNCTION(handlebars)
{
    handlebars_globals->handlebars_last_error = NULL;
}

static PHP_MINIT_FUNCTION(handlebars)
{
    int flags = CONST_CS | CONST_PERSISTENT | CONST_CT_SUBST;
    
    REGISTER_LONG_CONSTANT("HANDLEBARS_COMPILER_FLAG_NONE", handlebars_compiler_flag_none, flags);
    REGISTER_LONG_CONSTANT("HANDLEBARS_COMPILER_FLAG_USE_DEPTHS", handlebars_compiler_flag_use_depths, flags);
    REGISTER_LONG_CONSTANT("HANDLEBARS_COMPILER_FLAG_STRING_PARAMS", handlebars_compiler_flag_string_params, flags);
    REGISTER_LONG_CONSTANT("HANDLEBARS_COMPILER_FLAG_TRACK_IDS", handlebars_compiler_flag_track_ids, flags);
    REGISTER_LONG_CONSTANT("HANDLEBARS_COMPILER_FLAG_KNOWN_HELPERS_ONLY", handlebars_compiler_flag_known_helpers_only, flags);
    REGISTER_LONG_CONSTANT("HANDLEBARS_COMPILER_FLAG_COMPAT", handlebars_compiler_flag_compat, flags);
    REGISTER_LONG_CONSTANT("HANDLEBARS_COMPILER_FLAG_ALL", handlebars_compiler_flag_all, flags);
    
    return SUCCESS;
}

static PHP_RSHUTDOWN_FUNCTION(handlebars)
{
    if( HANDLEBARS_G(handlebars_last_error) ) {
        efree(HANDLEBARS_G(handlebars_last_error));
        HANDLEBARS_G(handlebars_last_error) = NULL;
    }
    
    return SUCCESS;
}

static PHP_MINFO_FUNCTION(handlebars)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "Version", PHP_HANDLEBARS_VERSION);
    php_info_print_table_row(2, "Released", PHP_HANDLEBARS_RELEASE);
    php_info_print_table_row(2, "Authors", PHP_HANDLEBARS_AUTHORS);
    // @todo make spec version from libhandlebars function
    php_info_print_table_row(2, "Spec Version", PHP_HANDLEBARS_SPEC);
    php_info_print_table_row(2, "libhandlebars Version", handlebars_version_string());
    php_info_print_table_end();
}

/* }}} ---------------------------------------------------------------------- */
/* {{{ Argument Info -------------------------------------------------------- */

ZEND_BEGIN_ARG_INFO_EX(handlebars_error_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(handlebars_lex_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, tmpl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(handlebars_lex_print_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, tmpl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(handlebars_parse_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, tmpl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(handlebars_parse_print_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, tmpl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(handlebars_compile_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, tmpl)
    ZEND_ARG_INFO(0, flags)
    ZEND_ARG_ARRAY_INFO(0, knownHelpers, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(handlebars_compile_print_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, tmpl)
    ZEND_ARG_INFO(0, flags)
    ZEND_ARG_ARRAY_INFO(0, knownHelpers, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(handlebars_version_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

/* }}} ---------------------------------------------------------------------- */
/* {{{ Function Entry ------------------------------------------------------- */

static const zend_function_entry handlebars_functions[] = {
    PHP_FE(handlebars_error, handlebars_error_args)
    PHP_FE(handlebars_lex, handlebars_lex_args)
    PHP_FE(handlebars_lex_print, handlebars_lex_print_args)
    PHP_FE(handlebars_parse, handlebars_parse_args)
    PHP_FE(handlebars_parse_print, handlebars_parse_print_args)
    PHP_FE(handlebars_compile, handlebars_compile_args)
    PHP_FE(handlebars_compile_print, handlebars_compile_print_args)
    PHP_FE(handlebars_version, handlebars_version_args)
    PHP_FE_END
};

/* }}} ---------------------------------------------------------------------- */
/* {{{ Module Entry --------------------------------------------------------- */

zend_module_entry handlebars_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_HANDLEBARS_NAME,                /* Name */
    handlebars_functions,               /* Functions */
    PHP_MINIT(handlebars),              /* MINIT */
    NULL,                               /* MSHUTDOWN */
    NULL,                               /* RINIT */
    PHP_RSHUTDOWN(handlebars),          /* RSHUTDOWN */
    PHP_MINFO(handlebars),              /* MINFO */
    PHP_HANDLEBARS_VERSION,             /* Version */
    PHP_MODULE_GLOBALS(handlebars),
    PHP_GINIT(handlebars),
    NULL,
    NULL,
    STANDARD_MODULE_PROPERTIES_EX
};

#ifdef COMPILE_DL_HANDLEBARS 
    ZEND_GET_MODULE(handlebars)      // Common for all PHP extensions which are build as shared modules  
#endif

/* }}} ---------------------------------------------------------------------- */
