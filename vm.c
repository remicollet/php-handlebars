
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "Zend/zend_API.h"
#include "Zend/zend_constants.h"
#include "Zend/zend_exceptions.h"
#include "main/php.h"
#include "ext/standard/basic_functions.h"
#include "ext/standard/php_filestat.h"

#include "handlebars.h"
#include "handlebars_private.h"
#include "handlebars_memory.h"

#include "handlebars_cache.h"
#include "handlebars_compiler.h"
#include "handlebars_helpers.h"
#include "handlebars_opcodes.h"
#include "handlebars_opcode_serializer.h"
#include "handlebars_string.h"
#include "handlebars_value.h"
#include "handlebars_vm.h"
#include "handlebars.tab.h"
#include "handlebars.lex.h"

#include "php5to7.h"
#include "php_handlebars.h"



/* {{{ Variables & Prototypes */
zend_class_entry * HandlebarsVM_ce_ptr;
static zend_object_handlers HandlebarsVM_obj_handlers;

struct php_handlebars_vm_obj {
#ifndef ZEND_ENGINE_3
    zend_object std;
#endif
    struct handlebars_context * context;
    struct handlebars_value * helpers;
    struct handlebars_value * partials;
#ifdef ZEND_ENGINE_3
    zend_object std;
#endif
};
/* }}} Variables & Prototypes */

/* {{{ Z_HANDLEBARS_VM_P */
#ifdef ZEND_ENGINE_3
static inline struct php_handlebars_vm_obj * php_handlebars_vm_fetch_object(zend_object *obj) {
    return (struct php_handlebars_vm_obj *)((char*)(obj) - XtOffsetOf(struct php_handlebars_vm_obj, std));
}
#define Z_HANDLEBARS_VM_P(zv) php_handlebars_vm_fetch_object(Z_OBJ_P((zv)))
#else
#define Z_HANDLEBARS_VM_P(zv) zend_object_store_get_object(zv TSRMLS_CC)
#endif
/* }}} */

/* {{{ php_handlebars_vm_obj_free */
#ifdef ZEND_ENGINE_3
static void php_handlebars_vm_obj_free(zend_object * object TSRMLS_DC)
{
    struct php_handlebars_vm_obj * obj = php_handlebars_vm_fetch_object(object TSRMLS_CC);

    if( obj->helpers ) {
        handlebars_value_dtor(obj->helpers);
    }
    if( obj->partials ) {
        handlebars_value_dtor(obj->partials);
    }
    handlebars_context_dtor(obj->context);

    zend_object_std_dtor((zend_object *)object TSRMLS_CC);
}
#else
static void php_handlebars_vm_obj_free(void *object TSRMLS_DC)
{
    struct php_handlebars_vm_obj * obj = (struct php_handlebars_vm_obj *) object;

    if( obj->helpers ) {
        handlebars_value_dtor(obj->helpers);
    }
    if( obj->partials    ) {
        handlebars_value_dtor(obj->partials);
    }
    handlebars_context_dtor(obj->context);

    zend_object_std_dtor(&obj->std TSRMLS_CC);
    efree(object);
}
#endif
/* }}} */

/* php_handlebars_log */
static void php_handlebars_log(
        int argc,
        struct handlebars_value * argv[],
        struct handlebars_options * options
) {
    TSRMLS_FETCH();
    zval * z_vm = (zval *) options->vm->log_ctx;
    zval * logger = php5to7_zend_read_property(HandlebarsBaseImpl_ce_ptr, z_vm, ZEND_STRL("logger"), 1);
    char * message;
    strsize_t message_len;
    int i;

    // Generate message
    message = handlebars_talloc_strdup(HANDLEBARS_G(root), "");
    for (i = 0; i < argc; i++) {
        char *tmp = handlebars_value_dump(argv[i], 0);
        message = handlebars_talloc_asprintf_append_buffer(message, "%s ", tmp);
        handlebars_talloc_free(tmp);
    }
    message_len = talloc_array_length(message) - 1;

    if( logger && Z_TYPE_P(logger) == IS_OBJECT ) {
        // @todo Look up log level
        struct handlebars_value * level = options->hash ? handlebars_value_map_str_find(options->hash, HBS_STRL("level")) : NULL;
        const char * level_str = level && level->type == HANDLEBARS_VALUE_TYPE_STRING ? level->v.string->val : "info";

        do {
#ifdef ZEND_ENGINE_3
            zval z_fn;
            zval z_ret;
            zval z_args[2];
            ZVAL_STRING(&z_fn, level_str);
            ZVAL_STRINGL(&z_args[0], message, message_len);
            array_init(&z_args[1]);
            call_user_function(&Z_OBJCE_P(logger)->function_table, logger, &z_fn, &z_ret, 2, z_args TSRMLS_CC);
            zval_ptr_dtor(&z_args[1]);
            zval_ptr_dtor(&z_args[0]);
            zval_ptr_dtor(&z_fn);
            zval_ptr_dtor(&z_ret);
#else
            zval * z_fn;
            zval * z_ret;
            zval * z_args[2];
            MAKE_STD_ZVAL(z_fn);
            MAKE_STD_ZVAL(z_ret);
            MAKE_STD_ZVAL(z_args[0]);
            MAKE_STD_ZVAL(z_args[1]);
            ZVAL_STRINGL(z_args[0], message, message_len, 1);
            array_init(z_args[1]);
            ZVAL_STRING(z_fn, level_str, 1);
            call_user_function(&Z_OBJCE_P(logger)->function_table, &logger, z_fn, z_ret, 2, z_args TSRMLS_CC);
            zval_ptr_dtor(&z_args[1]);
            zval_ptr_dtor(&z_args[0]);
            zval_ptr_dtor(&z_fn);
            zval_ptr_dtor(&z_ret);
#endif
        } while(0);
    } else {
        _php_error_log_ex(4, message, message_len, NULL, NULL TSRMLS_CC);
    }

    handlebars_talloc_free(message);
}
/* */

/* {{{ php_handlebars_vm_obj_create */
static inline void php_handlebars_vm_obj_create_common(struct php_handlebars_vm_obj *obj TSRMLS_DC)
{
    obj->context = handlebars_context_ctor_ex(HANDLEBARS_G(root));
    obj->helpers = handlebars_value_ctor(obj->context);
    handlebars_value_map_init(obj->helpers);
    obj->partials = handlebars_value_ctor(obj->context);
    handlebars_value_map_init(obj->partials);
}
#ifdef ZEND_ENGINE_3
static zend_object * php_handlebars_vm_obj_create(zend_class_entry * ce)
{
    struct php_handlebars_vm_obj *obj;

    obj = ecalloc(1, sizeof(*obj) + zend_object_properties_size(ce));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &HandlebarsVM_obj_handlers;
    php_handlebars_vm_obj_create_common(obj TSRMLS_CC);

    return &obj->std;
}
#else
zend_object_value php_handlebars_vm_obj_create(zend_class_entry *ce TSRMLS_DC)
{
    zend_object_value retval;
    struct php_handlebars_vm_obj *obj;

    obj = ecalloc(1, sizeof(struct php_handlebars_vm_obj));
    zend_object_std_init(&obj->std, ce TSRMLS_CC);

#if PHP_VERSION_ID < 50399
    zval *tmp;
    zend_hash_copy(obj->std.properties, &ce->default_properties, (copy_ctor_func_t) zval_property_ctor, (void *) &tmp, sizeof(zval *));
#else
    object_properties_init(&obj->std, ce);
#endif

    retval.handle = zend_objects_store_put(obj, (zend_objects_store_dtor_t)zend_objects_destroy_object, (zend_objects_free_object_storage_t)php_handlebars_vm_obj_free, NULL TSRMLS_CC);
    retval.handlers = &HandlebarsVM_obj_handlers;
    php_handlebars_vm_obj_create_common(obj TSRMLS_CC);

    return retval;
}
#endif
/* }}} */

/* {{{ php_handlebars_fetch_known_helpers */
void php_handlebars_fetch_known_helpers(struct handlebars_compiler * compiler, zval * helpers TSRMLS_DC)
{
    HashTable * data_hash = NULL;
    long num;
    long idx = 0;
    char ** known_helpers;

    if( Z_TYPE_P(helpers) == IS_ARRAY ) {
        data_hash = Z_ARRVAL_P(helpers);
    } else if( Z_TYPE_P(helpers) == IS_OBJECT && Z_OBJ_HT_P(helpers)->get_properties != NULL ) {
        data_hash = Z_OBJ_HT_P(helpers)->get_properties(helpers TSRMLS_CC);
    } else {
        return;
    }

    // @todo merge with existing helpers?

    num = zend_hash_num_elements(data_hash);
    known_helpers = handlebars_talloc_array(compiler, char *, num + 1);

    do {
#ifdef ZEND_ENGINE_3
        zend_string *key;
        zend_ulong index;
        ZEND_HASH_FOREACH_KEY(data_hash, index, key) {
            if( key ) {
                known_helpers[idx++] = handlebars_talloc_strndup(known_helpers, ZSTR_VAL(key), ZSTR_LEN(key));
            } else {
                (void) index;
            }
        } ZEND_HASH_FOREACH_END();
#else
        HashPosition data_pointer = NULL;
        zval ** data_entry = NULL;
        char * key;
        uint key_len;
        ulong index;
        zend_hash_internal_pointer_reset_ex(data_hash, &data_pointer);
        while( zend_hash_get_current_data_ex(data_hash, (void**) &data_entry, &data_pointer) == SUCCESS ) {
            if (zend_hash_get_current_key_ex(data_hash, &key, &key_len, &index, 0, &data_pointer) == HASH_KEY_IS_STRING) {
                known_helpers[idx++] = handlebars_talloc_strndup(known_helpers, key, key_len);
            }
            zend_hash_move_forward_ex(data_hash, &data_pointer);
        }
#endif
    } while(0);

    known_helpers[idx++] = 0;
    compiler->known_helpers = (const char **) known_helpers;
}
/* }}} php_handlebars_fetch_known_helpers */

static void php_handlebars_vm_set_helpers(zval * _this_zval, zval * helpers TSRMLS_DC)
{
    jmp_buf buf;
    struct php_handlebars_vm_obj * intern = Z_HANDLEBARS_VM_P(_this_zval);
    struct handlebars_context * context = intern->context;
    php_handlebars_try(HandlebarsRuntimeException_ce_ptr, context, &buf);
    if( intern->helpers ) {
        handlebars_value_dtor(intern->helpers);
    }
    intern->helpers = handlebars_value_from_zval(HBSCTX(context), helpers TSRMLS_CC);
    zend_update_property(Z_OBJCE_P(_this_zval), _this_zval, ZEND_STRL("helpers"), helpers TSRMLS_CC);
done:
    context->e->jmp = NULL;
}
PHP_METHOD(HandlebarsVM, setHelpers)
{
    zval * _this_zval;
    zval * helpers;

#ifndef FAST_ZPP
    if( zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), (char *) "OO",
            &_this_zval, HandlebarsVM_ce_ptr, &helpers, HandlebarsRegistry_ce_ptr) == FAILURE ) {
        return;
    }
#else
    _this_zval = getThis();
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(helpers, HandlebarsRegistry_ce_ptr)
    ZEND_PARSE_PARAMETERS_END();
#endif
    php_handlebars_vm_set_helpers(_this_zval, helpers TSRMLS_CC);
}

static void php_handlebars_vm_set_partials(zval * _this_zval, zval * partials TSRMLS_DC)
{
    jmp_buf buf;
    struct php_handlebars_vm_obj * intern = Z_HANDLEBARS_VM_P(_this_zval);
    struct handlebars_context * context = intern->context;
    php_handlebars_try(HandlebarsRuntimeException_ce_ptr, context, &buf);
    if( intern->partials ) {
        handlebars_value_dtor(intern->partials);
    }
    intern->partials = handlebars_value_from_zval(HBSCTX(context), partials TSRMLS_CC);
    zend_update_property(Z_OBJCE_P(_this_zval), _this_zval, ZEND_STRL("partials"), partials TSRMLS_CC);
done:
    context->e->jmp = NULL;
}

PHP_METHOD(HandlebarsVM, setPartials)
{
    zval * _this_zval;
    zval * partials;

#ifndef FAST_ZPP
    if( zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), (char *) "OO",
            &_this_zval, HandlebarsVM_ce_ptr, &partials, HandlebarsRegistry_ce_ptr) == FAILURE ) {
        return;
    }
#else
    _this_zval = getThis();
    ZEND_PARSE_PARAMETERS_START(1, 1)
            Z_PARAM_OBJECT_OF_CLASS(partials, HandlebarsRegistry_ce_ptr)
    ZEND_PARSE_PARAMETERS_END();
#endif

    php_handlebars_vm_set_partials(_this_zval, partials TSRMLS_CC);
}

PHP_METHOD(HandlebarsVM, __construct)
{
    zval * _this_zval;
    zval * z_options = NULL;

#ifndef FAST_ZPP
    if( zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O|z",
            &_this_zval, HandlebarsVM_ce_ptr, &z_options) == FAILURE) {
        return;
    }
#else
    _this_zval = getThis();
    ZEND_PARSE_PARAMETERS_START(0, 1)
            Z_PARAM_OPTIONAL
            Z_PARAM_ARRAY(z_options)
    ZEND_PARSE_PARAMETERS_END();
#endif

    if( z_options && Z_TYPE_P(z_options) == IS_ARRAY ) {
        HashTable * ht = Z_ARRVAL_P(z_options);
        zval * helpers = php5to7_zend_hash_find(ht, ZEND_STRL("helpers"));
        zval * partials = php5to7_zend_hash_find(ht, ZEND_STRL("partials"));
        zval * logger = php5to7_zend_hash_find(ht, ZEND_STRL("logger"));
        if( helpers ) {
            php_handlebars_vm_set_helpers(_this_zval, helpers TSRMLS_CC);
        }
        if( partials ) {
            php_handlebars_vm_set_partials(_this_zval, partials TSRMLS_CC);
        }
        if( logger ) {
            // @todo check type
            zend_update_property(Z_OBJCE_P(_this_zval), _this_zval, ZEND_STRL("logger"), logger TSRMLS_CC);
        }
    }
}

PHP_METHOD(HandlebarsVM, render)
{
    zval * _this_zval;
    char * tmpl_str;
    strsize_t tmpl_len;
    zval * z_context = NULL;
    zval * z_options = NULL;
    TALLOC_CTX * mctx;
    struct handlebars_cache * cache = NULL;
    struct handlebars_module * module = NULL;
    struct handlebars_context * ctx = NULL;
    struct handlebars_parser * parser;
    struct handlebars_compiler * compiler;
    struct handlebars_value * context;
    jmp_buf buf;
    bool from_cache = false;

#ifndef FAST_ZPP
    if( zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os|zz",
            &_this_zval, HandlebarsVM_ce_ptr, &tmpl_str, &tmpl_len, &z_context, &z_options) == FAILURE ) {
        return;
    }
#else
    _this_zval = getThis();
    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STRING(tmpl_str, tmpl_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(z_context)
        Z_PARAM_ZVAL(z_options)
    ZEND_PARSE_PARAMETERS_END();
#endif

#ifdef ZEND_ENGINE_3
    // Dereference zval
    if( z_context && Z_TYPE_P(z_context) == IS_REFERENCE ) {
        ZVAL_DEREF(z_context);
    }
#endif

    struct php_handlebars_vm_obj * intern = Z_HANDLEBARS_VM_P(_this_zval);
    if( HANDLEBARS_G(pool_size) > 0 ) {
        mctx = talloc_pool(intern->context, HANDLEBARS_G(pool_size));
    } else {
        mctx = talloc_new(intern->context);
    }
    ctx = handlebars_context_ctor_ex(mctx);

    struct handlebars_vm * vm = handlebars_vm_ctor(ctx);
    cache = HANDLEBARS_G(cache);

    vm->cache = cache;
    vm->helpers = intern->helpers;
    vm->partials = intern->partials;
    vm->log_func = &php_handlebars_log;
    vm->log_ctx = _this_zval;

    struct handlebars_string * tmpl = handlebars_string_ctor(HBSCTX(vm), tmpl_str, tmpl_len);

    // Lookup cache entry
    if( cache && (module = handlebars_cache_find(cache, tmpl)) ) {
        // Use cached
        from_cache = true;
    } else {
        php_handlebars_try(HandlebarsRuntimeException_ce_ptr, ctx, &buf);
        parser = handlebars_parser_ctor(ctx);
        compiler = handlebars_compiler_ctor(ctx);

        // Parse
        php_handlebars_try(HandlebarsParseException_ce_ptr, parser, &buf);
        parser->tmpl = tmpl;
        handlebars_parse(parser);

        // Compile
        php_handlebars_try(HandlebarsCompileException_ce_ptr, compiler, &buf);
        php_handlebars_process_options_zval(compiler, vm, z_options TSRMLS_CC);
        /*if( z_helpers ) {
            php_handlebars_fetch_known_helpers(compiler, z_helpers TSRMLS_CC);
        }*/
        handlebars_compiler_compile(compiler, parser->program);

        module = handlebars_program_serialize(HBSCTX(vm), compiler->program);

        // Save cache entry
        if( cache ) {
            handlebars_cache_add(cache, tmpl, module);
        }
    }

    // Make context
    php_handlebars_try(HandlebarsRuntimeException_ce_ptr, vm, &buf);
    if( z_context ) {
        context = handlebars_value_from_zval(HBSCTX(vm), z_context TSRMLS_CC);
    } else {
        context = handlebars_value_ctor(HBSCTX(vm));
    }

    // Execute
    vm->flags = module->flags;
    handlebars_vm_execute(vm, module, context);

    if( vm->buffer && !EG(exception) ) {
        PHP5TO7_RETVAL_STRINGL(vm->buffer->val, vm->buffer->len);
    }

done:
    if( from_cache ) {
        cache->release(cache, tmpl, module);
    }
    handlebars_vm_dtor(vm);
    handlebars_talloc_free(mctx);
}

PHP_METHOD(HandlebarsVM, renderFile)
{
    zval * _this_zval;
    char * filename_str;
    strsize_t filename_len;
    zval * z_context = NULL;
    zval * z_options = NULL;
    void * mctx = NULL;
    struct handlebars_cache * cache = NULL;
    struct handlebars_module * module = NULL;
    struct handlebars_context * ctx = NULL;
    struct handlebars_parser * parser;
    struct handlebars_compiler * compiler;
    struct handlebars_value * context;
    jmp_buf buf;
    bool from_cache = false;

#ifndef FAST_ZPP
    if( zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os|zz",
            &_this_zval, HandlebarsVM_ce_ptr, &filename_str, &filename_len, &z_context, &z_options) == FAILURE ) {
        return;
    }
#else
    _this_zval = getThis();
    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STRING(filename_str, filename_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(z_context)
        Z_PARAM_ZVAL(z_options)
    ZEND_PARSE_PARAMETERS_END();
#endif

#if PHP_MAJOR_VERSION >= 7
    // Dereference zval
    if( z_context && Z_TYPE_P(z_context) == IS_REFERENCE ) {
        ZVAL_DEREF(z_context);
    }
#endif

    struct php_handlebars_vm_obj * intern = Z_HANDLEBARS_VM_P(_this_zval);
    if( HANDLEBARS_G(pool_size) > 0 ) {
        mctx = talloc_pool(intern->context, HANDLEBARS_G(pool_size));
    } else {
        mctx = talloc_new(intern->context);
    }
    ctx = handlebars_context_ctor_ex(mctx);

    struct handlebars_vm * vm = handlebars_vm_ctor(ctx);
    cache = HANDLEBARS_G(cache);

    vm->cache = cache;
    vm->helpers = intern->helpers;
    vm->partials = intern->partials;
    vm->log_func = &php_handlebars_log;
    vm->log_ctx = _this_zval;

    struct handlebars_string * filename = handlebars_string_ctor(HBSCTX(vm), filename_str, filename_len);

    // Lookup cache entry
    if( cache && (module = handlebars_cache_find(cache, filename)) ) {
        // Check if too old
        if( HANDLEBARS_G(cache_stat) ) {
            zval zstat;
            ZVAL_LONG(&zstat, 0);
            php_stat(filename_str, filename_len, FS_MTIME, &zstat TSRMLS_CC);
            if( Z_LVAL(zstat) > module->ts ) { // possibly not portable
                cache->release(cache, filename, module);
                from_cache = false;
                module = NULL;
            }
        }

        // Use cached
        from_cache = true;
    }

    if( !module ) {
        // Read file
        php_stream *stream;
        struct handlebars_string * tmpl;

        stream = php_stream_open_wrapper_ex(filename_str, "rb", REPORT_ERRORS, NULL, NULL);
        if( !stream ) {
            RETVAL_FALSE;
            goto done;
        }

#ifdef ZEND_ENGINE_3
        zend_string *contents = php_stream_copy_to_mem(stream, PHP_STREAM_COPY_ALL, 0);
        php_stream_close(stream);
        if( contents != NULL) {
            tmpl = handlebars_string_ctor(HBSCTX(vm), contents->val, contents->len);
        } else {
            RETVAL_FALSE;
            goto done;
        }
#else
        char * contents_str;
        strsize_t contents_len = php_stream_copy_to_mem(stream, &contents_str, PHP_STREAM_COPY_ALL, 0);
        php_stream_close(stream);
        if( contents_len ) {
            tmpl = handlebars_string_ctor(HBSCTX(vm), contents_str, contents_len);
            efree(contents_str);
        } else {
            RETVAL_FALSE;
            goto done;
        }
#endif

        //ctx = handlebars_context_ctor_ex(HANDLEBARS_G(root));
        php_handlebars_try(HandlebarsRuntimeException_ce_ptr, ctx, &buf);
        parser = handlebars_parser_ctor(ctx);
        compiler = handlebars_compiler_ctor(ctx);

        // Parse
        php_handlebars_try(HandlebarsParseException_ce_ptr, parser, &buf);
        parser->tmpl = tmpl;
        handlebars_parse(parser);

        // Compile
        php_handlebars_try(HandlebarsCompileException_ce_ptr, compiler, &buf);
        php_handlebars_process_options_zval(compiler, vm, z_options TSRMLS_CC);
        /*if( z_helpers ) {
            php_handlebars_fetch_known_helpers(compiler, z_helpers TSRMLS_CC);
        }*/
        handlebars_compiler_compile(compiler, parser->program);

        module = handlebars_program_serialize(HBSCTX(vm), compiler->program);

        // Save cache entry
        if( cache ) {
            handlebars_cache_add(cache, filename, module);
            //handlebars_cache_add(cache, tmpl, program);
        }
    }

    // Make context
    php_handlebars_try(HandlebarsRuntimeException_ce_ptr, vm, &buf);
    context = handlebars_value_from_zval(HBSCTX(vm), z_context TSRMLS_CC);

    // Execute
    vm->flags = module->flags;
    handlebars_vm_execute(vm, module, context);

    if( vm->buffer && !EG(exception) ) {
        PHP5TO7_RETVAL_STRINGL(vm->buffer->val, vm->buffer->len);
    }

done:
    if( from_cache ) {
        cache->release(cache, filename, module);
    }
    handlebars_vm_dtor(vm);
    handlebars_talloc_free(mctx);
}

/* {{{ Argument Info */
ZEND_BEGIN_ARG_INFO_EX(HandlebarsVM_construct_args, ZEND_SEND_BY_VAL, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, options, 1)
ZEND_END_ARG_INFO()
/* }}} Argument Info */


/* {{{ HandlebarsVM methods */
static zend_function_entry HandlebarsVM_methods[] = {
    PHP_ME(HandlebarsVM, __construct, HandlebarsVM_construct_args, ZEND_ACC_PUBLIC)
    PHP_ME(HandlebarsVM, setHelpers, HandlebarsImpl_setHelpers_args, ZEND_ACC_PUBLIC)
    PHP_ME(HandlebarsVM, setPartials, HandlebarsImpl_setPartials_args, ZEND_ACC_PUBLIC)
    PHP_ME(HandlebarsVM, render, HandlebarsImpl_render_args, ZEND_ACC_PUBLIC)
    PHP_ME(HandlebarsVM, renderFile, HandlebarsImpl_renderFile_args, ZEND_ACC_PUBLIC)
    PHP_FE_END
};
/* }}} HandlebarsVM methods */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(handlebars_vm)
{
    zend_class_entry ce;

    memcpy(&HandlebarsVM_obj_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
#ifdef ZEND_ENGINE_3
    HandlebarsVM_obj_handlers.offset = XtOffsetOf(struct php_handlebars_vm_obj, std);
    HandlebarsVM_obj_handlers.free_obj = php_handlebars_vm_obj_free;
#endif
    HandlebarsVM_obj_handlers.clone_obj = NULL;

    INIT_CLASS_ENTRY(ce, "Handlebars\\VM", HandlebarsVM_methods);
    HandlebarsVM_ce_ptr = php5to7_register_internal_class_ex(&ce, HandlebarsBaseImpl_ce_ptr);
    HandlebarsVM_ce_ptr ->create_object = php_handlebars_vm_obj_create;

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