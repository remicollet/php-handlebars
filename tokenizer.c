
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "Zend/zend_API.h"
#include "Zend/zend_exceptions.h"
#include "main/php.h"

#include "handlebars.h"
#include "handlebars_memory.h"
#include "handlebars_token.h"
#include "handlebars_token_list.h"
#include "handlebars_token_printer.h"
#include "handlebars.tab.h"
#include "handlebars.lex.h"

#include "php5to7.h"
#include "php_handlebars.h"

/* {{{ Variables & Prototypes */
zend_class_entry * HandlebarsTokenizer_ce_ptr;
/* }}} Variables & Prototypes */

/* {{{ proto mixed Handlebars\Tokenizer::lex(string tmpl) */
static inline void php_handlebars_lex(INTERNAL_FUNCTION_PARAMETERS, short print)
{
    char * tmpl;
    strsize_t tmpl_len;
    struct handlebars_context * ctx;
    struct handlebars_parser * parser;
    struct handlebars_token_list * list;
    struct handlebars_token_list_item * el = NULL;
    struct handlebars_token_list_item * tmp = NULL;
    char * output;
    char * errmsg;
    volatile struct {
        zend_class_entry * ce;
    } ex;
    jmp_buf buf;
    _DECLARE_ZVAL(child);

    ex.ce = HandlebarsRuntimeException_ce_ptr;

#ifndef FAST_ZPP
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &tmpl, &tmpl_len) == FAILURE ) {
        return;
    }
#else
    ZEND_PARSE_PARAMETERS_START(1, 1)
	    Z_PARAM_STRING(tmpl, tmpl_len)
    ZEND_PARSE_PARAMETERS_END();
#endif

    ctx = handlebars_context_ctor();

    php_handlebars_try(HandlebarsRuntimeException_ce_ptr, ctx, &buf);
    parser = handlebars_parser_ctor(ctx);

    // Lex
    ex.ce = HandlebarsParseException_ce_ptr;
    parser->tmpl = tmpl;
    php_handlebars_try(HandlebarsParseException_ce_ptr, parser, &buf);
    list = handlebars_lex(parser);

    // Print or convert to zval
    php_handlebars_try(HandlebarsRuntimeException_ce_ptr, parser, &buf);
    if( print ) {
        output = handlebars_token_list_print(list, 0);
        PHP5TO7_RETVAL_STRING(output);
    } else {
        array_init(return_value);
        handlebars_token_list_foreach(list, el, tmp) {
            _ALLOC_INIT_ZVAL(child);
            php_handlebars_token_ctor(el->data, child TSRMLS_CC);
            add_next_index_zval(return_value, child);
        }
    }

done:
    handlebars_context_dtor(ctx);
}

PHP_METHOD(HandlebarsTokenizer, lex)
{
    php_handlebars_lex(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}

PHP_METHOD(HandlebarsTokenizer, lexPrint)
{
    php_handlebars_lex(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}
/* }}} Handlebars\Tokenizer::lex */

/* {{{ Argument Info */
ZEND_BEGIN_ARG_INFO_EX(HandlebarsTokenizer_lex_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, tmpl)
ZEND_END_ARG_INFO()
/* }}} Argument Info */

/* {{{ HandlebarsTokenizer methods */
static zend_function_entry HandlebarsTokenizer_methods[] = {
    PHP_ME(HandlebarsTokenizer, lex, HandlebarsTokenizer_lex_args, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(HandlebarsTokenizer, lexPrint, HandlebarsTokenizer_lex_args, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    { NULL, NULL, NULL }
};
/* }}} HandlebarsTokenizer methods */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(handlebars_tokenizer)
{
    zend_class_entry ce;

    INIT_CLASS_ENTRY(ce, "Handlebars\\Tokenizer", HandlebarsTokenizer_methods);
    HandlebarsTokenizer_ce_ptr = zend_register_internal_class(&ce TSRMLS_CC);

    return SUCCESS;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: fdm=marker
 * vim: et sw=4 ts=4
 */
