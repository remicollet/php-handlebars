
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <talloc.h>

#include "Zend/zend_API.h"
#include "Zend/zend_constants.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"
#include "main/php.h"

#include "php_handlebars.h"

#include "handlebars_helpers.h"
#include "handlebars_value.h"
#include "handlebars_vm.h"

/* {{{ Variables & Prototypes */
PHP_HANDLEBARS_API zend_class_entry * HandlebarsOptions_ce_ptr;
static zend_object_handlers HandlebarsOptions_obj_handlers;
static HashTable HandlebarsOptions_prop_handlers;
static zend_string *INTERNED_NAME;
static zend_string *INTERNED_FN;
static zend_string *INTERNED_INVERSE;
static zend_string *INTERNED_SCOPE;
static zend_string *INTERNED_HASH;
static zend_string *INTERNED_DATA;
static zend_string *INTERNED_BLOCK_PARAMS;
/* }}} Variables & Prototypes */

struct php_handlebars_options_obj {
    struct handlebars_options options;
    zend_object_handlers * std_hnd;
    zend_object std;
};

struct hbs_prop_handlers {
    zend_object_read_property_t read_property;
    zend_object_has_property_t has_property;
};

#if PHP_MAJOR_VERSION >= 8
#define HAS_PROPERTY_ARGS zend_object *object, zend_string *member, int check_empty, void **cache_slot
#define READ_PROPERTY_ARGS zend_object *object, zend_string *member, int type, void **cache_slot, zval *rv
#else
#define HAS_PROPERTY_ARGS zval *object, zval *member, int check_empty, void **cache_slot
#define READ_PROPERTY_ARGS zval *object, zval *member, int type, void **cache_slot, zval *rv
#endif
#define READ_PROPERTY_ARGS_PASSTHRU object, member, type, cache_slot, rv

static inline void register_prop_handler(const char * name, zend_object_read_property_t read_property)
{
    struct hbs_prop_handlers hnd;
    hnd.read_property = read_property;
    hnd.has_property = NULL;
    zend_hash_str_update_mem(&HandlebarsOptions_prop_handlers, name, strlen(name), &hnd, sizeof(struct hbs_prop_handlers));
}

/* {{{ Argument Info */
ZEND_BEGIN_ARG_INFO_EX(HandlebarsOptions_construct_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_ARRAY_INFO(0, props, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(HandlebarsOptions_offsetExists_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, prop)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(HandlebarsOptions_offsetSet_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 2)
    ZEND_ARG_INFO(0, prop)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(HandlebarsOptions_fn_args, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()
/* }}} Argument Info */

/* {{{ Z_HANDLEBARS_OPTIONS_P */
static inline struct php_handlebars_options_obj * php_handlebars_options_fetch_object(zend_object *obj) {
    return (struct php_handlebars_options_obj *)((char*)(obj) - XtOffsetOf(struct php_handlebars_options_obj, std));
}
#define Z_HBS_OPTIONS_P(zv) php_handlebars_options_fetch_object(Z_OBJ_P((zv)))
/* }}} */

/* {{{ php_handlebars_options_obj_free */
static inline void php_handlebars_options_obj_free_common(struct php_handlebars_options_obj * intern)
{
    // Note: these can be freed by talloc without being released by options_deinit during an exception
    /*
    handlebars_value_try_delref(intern->options.scope);
    handlebars_value_try_delref(intern->options.hash);
    handlebars_value_try_delref(intern->options.data);
    */
}
static void php_handlebars_options_obj_free(zend_object * object)
{
    struct php_handlebars_options_obj * payload = php_handlebars_options_fetch_object(object);
    php_handlebars_options_obj_free_common(payload);
    zend_object_std_dtor((zend_object *)object);
}
/* }}} */

/* {{{ php_handlebars_options_obj_create */
static zend_object * php_handlebars_options_obj_create(zend_class_entry * ce)
{
    struct php_handlebars_options_obj *obj;

    obj = ecalloc(1, sizeof(*obj) + zend_object_properties_size(ce));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &HandlebarsOptions_obj_handlers;

    obj->std_hnd = zend_get_std_object_handlers();
    obj->options.program = -1;
    obj->options.inverse = -1;

    return &obj->std;
}
/* }}} */

/* {{{ php_handlebars_options_ctor */
PHP_HANDLEBARS_API void php_handlebars_options_ctor(
        struct handlebars_options * options,
        zval * z_options
) {
    struct php_handlebars_options_obj * intern;

    object_init_ex(z_options, HandlebarsOptions_ce_ptr);

    intern = Z_HBS_OPTIONS_P(z_options);
    intern->options = *options;
    handlebars_value_try_addref(intern->options.scope);
    handlebars_value_try_addref(intern->options.hash);
    handlebars_value_try_addref(intern->options.data);
}
/* }}} */

/* {{{ Object handlers */
static zval * hbs_read_name(READ_PROPERTY_ARGS)
{
#if PHP_MAJOR_VERSION >= 8
    struct php_handlebars_options_obj * intern = php_handlebars_options_fetch_object(object);
    if( intern->options.name ) {
        zval tmp = {0};
        ZVAL_STRINGL(&tmp, intern->options.name->val, intern->options.name->len);
        object->handlers->write_property(object, member, &tmp, NULL);
        intern->options.name = NULL;
    }
#else
    struct php_handlebars_options_obj * intern = Z_HBS_OPTIONS_P(object);
    if( intern->options.name ) {
        zval tmp;
        ZVAL_STRINGL(&tmp, intern->options.name->val, intern->options.name->len);
        zend_update_property_ex(Z_OBJCE_P(object), object, INTERNED_NAME, &tmp);
        intern->options.name = NULL;
    }
#endif
    return intern->std_hnd->read_property(READ_PROPERTY_ARGS_PASSTHRU);
}
static zval * hbs_read_program(READ_PROPERTY_ARGS)
{
#if PHP_MAJOR_VERSION >= 8
    struct php_handlebars_options_obj * intern = php_handlebars_options_fetch_object(object);
    if( intern->options.program >= 0 ) {
        zval tmp = {0};
        ZVAL_LONG(&tmp, intern->options.program);
        object->handlers->write_property(object, member, &tmp, NULL);
    }
#else
    struct php_handlebars_options_obj * intern = Z_HBS_OPTIONS_P(object);
    if( intern->options.program >= 0 ) {
        zval tmp;
        ZVAL_LONG(&tmp, intern->options.program);
        zend_update_property_ex(Z_OBJCE_P(object), object, INTERNED_FN, &tmp);
        // @todo clear?
    }
#endif
    return intern->std_hnd->read_property(READ_PROPERTY_ARGS_PASSTHRU);
}
static zval * hbs_read_inverse(READ_PROPERTY_ARGS)
{
#if PHP_MAJOR_VERSION >= 8
    struct php_handlebars_options_obj * intern = php_handlebars_options_fetch_object(object);
    if( intern->options.inverse >= 0 ) {
        zval tmp;
        ZVAL_LONG(&tmp, intern->options.inverse);
        object->handlers->write_property(object, member, &tmp, NULL);
    }
#else
    struct php_handlebars_options_obj * intern = Z_HBS_OPTIONS_P(object);
    if( intern->options.inverse >= 0 ) {
        zval tmp;
        ZVAL_LONG(&tmp, intern->options.inverse);
        zend_update_property_ex(Z_OBJCE_P(object), object, INTERNED_INVERSE, &tmp);
        // @todo clear?
    }
#endif
    return intern->std_hnd->read_property(READ_PROPERTY_ARGS_PASSTHRU);
}
static zval * hbs_read_scope(READ_PROPERTY_ARGS)
{
#if PHP_MAJOR_VERSION >= 8
    struct php_handlebars_options_obj * intern = php_handlebars_options_fetch_object(object);
    if( intern->options.scope ) {
        zval z_scope = {0};
        handlebars_value_to_zval(intern->options.scope, &z_scope);
        object->handlers->write_property(object, member, &z_scope, NULL);
        zval_ptr_dtor(&z_scope);
        intern->options.scope = NULL;
    }
#else
    struct php_handlebars_options_obj * intern = Z_HBS_OPTIONS_P(object);
    if( intern->options.scope ) {
        zval z_scope = {0};
        handlebars_value_to_zval(intern->options.scope, &z_scope);
        zend_update_property_ex(Z_OBJCE_P(object), object, INTERNED_SCOPE, &z_scope);
        zval_ptr_dtor(&z_scope);
        intern->options.scope = NULL;
    }
#endif
    return intern->std_hnd->read_property(READ_PROPERTY_ARGS_PASSTHRU);
}
static zval * hbs_read_hash(READ_PROPERTY_ARGS)
{
#if PHP_MAJOR_VERSION >= 8
    struct php_handlebars_options_obj * intern = php_handlebars_options_fetch_object(object);
    if( intern->options.hash ) {
        zval z_hash = {0};
        handlebars_value_to_zval(intern->options.hash, &z_hash);
        object->handlers->write_property(object, member, &z_hash, NULL);
        zval_ptr_dtor(&z_hash);
        intern->options.hash = NULL;
    }
#else
    struct php_handlebars_options_obj * intern = Z_HBS_OPTIONS_P(object);
    if( intern->options.hash ) {
        zval z_hash = {0};
        handlebars_value_to_zval(intern->options.hash, &z_hash);
        zend_update_property_ex(Z_OBJCE_P(object), object, INTERNED_HASH, &z_hash);
        zval_ptr_dtor(&z_hash);
        intern->options.hash = NULL;
    }
#endif
    return intern->std_hnd->read_property(READ_PROPERTY_ARGS_PASSTHRU);
}
static zval * hbs_read_data(READ_PROPERTY_ARGS)
{
#if PHP_MAJOR_VERSION >= 8
    struct php_handlebars_options_obj * intern = php_handlebars_options_fetch_object(object);
    if( intern->options.data ) {
        zval z_data = {0};
        handlebars_value_to_zval(intern->options.data, &z_data);
        object->handlers->write_property(object, member, &z_data, NULL);
        zval_ptr_dtor(&z_data);
        intern->options.data = NULL;
    }
#else
    struct php_handlebars_options_obj * intern = Z_HBS_OPTIONS_P(object);
    if( intern->options.data ) {
        zval z_data = {0};
        handlebars_value_to_zval(intern->options.data, &z_data);
        zend_update_property_ex(Z_OBJCE_P(object), object, INTERNED_DATA, &z_data);
        zval_ptr_dtor(&z_data);
        intern->options.data = NULL;
    }
#endif
    return intern->std_hnd->read_property(READ_PROPERTY_ARGS_PASSTHRU);
}
static zval *php_handlebars_options_object_read_property(READ_PROPERTY_ARGS)
{
#if PHP_MAJOR_VERSION >= 8
    struct php_handlebars_options_obj * intern = php_handlebars_options_fetch_object(object);
    struct hbs_prop_handlers * hnd = zend_hash_find_ptr(&HandlebarsOptions_prop_handlers, member);
    zval * ret;
    if( hnd ) {
        ret = hnd->read_property(object, member, type, cache_slot, rv);
    } else {
        ret = intern->std_hnd->read_property(object, member, type, cache_slot, rv);
    }
    // zend_string_release(member); // @todo needed?
#else
    zend_string *member_str = zval_get_string(member);
    struct php_handlebars_options_obj * intern = Z_HBS_OPTIONS_P(object);
    struct hbs_prop_handlers * hnd = zend_hash_find_ptr(&HandlebarsOptions_prop_handlers, member_str);
    zval * ret;
    if( hnd ) {
        ret = hnd->read_property(object, member, type, cache_slot, rv);
    } else {
        ret = intern->std_hnd->read_property(object, member, type, cache_slot, rv);
    }
    zend_string_release(member_str);
#endif
    return ret;
}
static int php_handlebars_options_object_has_property(HAS_PROPERTY_ARGS)
{
#if PHP_MAJOR_VERSION >= 8
    struct php_handlebars_options_obj * intern = php_handlebars_options_fetch_object(object);
#else
    struct php_handlebars_options_obj * intern = Z_HBS_OPTIONS_P(object);
#endif
    zval tmp = {0};
    php_handlebars_options_object_read_property(object, member, 0, cache_slot, &tmp);
    return intern->std_hnd->has_property(object, member, check_empty, cache_slot);
}
/* }}} Object handlers */

/* {{{ proto Handlebars\Options::__construct([array $props]) */
PHP_METHOD(HandlebarsOptions, __construct)
{
    zval * _this_zval = getThis();
    zval * props = NULL;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY(props)
    ZEND_PARSE_PARAMETERS_END();

    zval tmp;
    ZVAL_NULL(&tmp);
    zend_update_property_ex(Z_OBJCE_P(_this_zval), _this_zval, INTERNED_FN, &tmp);
    zend_update_property_ex(Z_OBJCE_P(_this_zval), _this_zval, INTERNED_INVERSE, &tmp);

    if( props && Z_TYPE_P(props) == IS_ARRAY ) {
        HashTable * ht = Z_ARRVAL_P(props);
        zend_string * key;
        zend_ulong index;
        zval * entry;

        ZEND_HASH_FOREACH_KEY_VAL(ht, index, key, entry) {
            if( key ) {
                zend_update_property(Z_OBJCE_P(_this_zval), _this_zval, ZSTR_VAL(key), ZSTR_LEN(key), entry);
            } else {
                (void) index;
            }
        } ZEND_HASH_FOREACH_END();
    }
}
/* }}} Handlebars\Options::__construct */

static inline void php_handlebars_options_call(INTERNAL_FUNCTION_PARAMETERS, short program)
{
    zval * _this_zval = getThis();
    zval * z_context = NULL;
    zval * z_options = NULL;
    zval * z_entry;
    struct php_handlebars_options_obj * intern;
    struct handlebars_vm * vm;
    long programGuid;
    struct handlebars_value * context;
    struct handlebars_value * data = NULL;
    struct handlebars_value * block_params = NULL;
    jmp_buf buf;
    jmp_buf * prev;

    ZEND_PARSE_PARAMETERS_START(0, 2)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(z_context)
        Z_PARAM_ZVAL(z_options)
    ZEND_PARSE_PARAMETERS_END();

    intern = Z_HBS_OPTIONS_P(_this_zval);
    vm = intern->options.vm;

    if( !vm ) {
        // This was probably constructed in user land
        zval * z_fn;
        if( program ) {
            //z_fn = zend_hash_str_find(Z_ARRVAL_P(z_options), ZEND_STRL("data"))
            z_fn = zend_read_property_ex(Z_OBJCE_P(_this_zval), _this_zval, INTERNED_FN, 0, NULL);
        } else {
            z_fn = zend_read_property_ex(Z_OBJCE_P(_this_zval), _this_zval, INTERNED_INVERSE, 0, NULL);
        }

        if( z_fn && Z_TYPE_P(z_fn) == IS_OBJECT ) {
            zval z_const;
            zval z_ret;
            zval * z_const_args = ecalloc(ZEND_NUM_ARGS(), sizeof(zval));
            if( ZEND_NUM_ARGS() >= 1 ) {
                //ZVAL_DUP(&z_const_args[0], z_context);
                z_const_args[0] = *z_context;
            }
            if( ZEND_NUM_ARGS() >= 2 ) {
                //ZVAL_DUP(&z_const_args[1], z_context);
                z_const_args[1] = *z_options;
            }
            ZVAL_NULL(&z_ret);
            ZVAL_STRING(&z_const, "__invoke");

            call_user_function(&Z_OBJCE_P(z_fn)->function_table, z_fn, &z_const, &z_ret, ZEND_NUM_ARGS(), z_const_args);

            RETVAL_ZVAL(&z_ret, 1, 1);
            zval_ptr_dtor(&z_const);
        } else {
            zend_throw_exception(HandlebarsRuntimeException_ce_ptr, "fn is not set", 0);
        }
        return;
    }

    if( program ) {
        programGuid = intern->options.program;
    } else {
        programGuid = intern->options.inverse;
    }

    if( programGuid < 0 ) {
        // nothing to do
        return;
    }

    // Context
    if( z_context ) {
        context = handlebars_value_from_zval(HBSCTX(vm), z_context);
    } else {
        context = handlebars_value_ctor(HBSCTX(vm));
    }

    // Options
    if( z_options && Z_TYPE_P(z_options) == IS_ARRAY ) {
        if( NULL != (z_entry = zend_hash_find(Z_ARRVAL_P(z_options), INTERNED_DATA)) ) {
            data = handlebars_value_from_zval(HBSCTX(vm), z_entry);
        }
        if( NULL != (z_entry = zend_hash_find(Z_ARRVAL_P(z_options), INTERNED_BLOCK_PARAMS)) ) {
            block_params = handlebars_value_from_zval(HBSCTX(vm), z_entry);
        }
        // @todo block params?
    }

    // Save jump buffer;
    prev = HBSCTX(vm)->e->jmp;
    php_handlebars_try(HandlebarsRuntimeException_ce_ptr, vm, &buf);

    // Execute
    struct handlebars_string * ret = handlebars_vm_execute_program_ex(vm, programGuid, context, data, block_params);
    RETVAL_STRINGL(ret->val, ret->len);
    talloc_free(ret);

done:
    HBSCTX(vm)->e->jmp = prev;
}

PHP_METHOD(HandlebarsOptions, fn)
{
    php_handlebars_options_call(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}

PHP_METHOD(HandlebarsOptions, inverse)
{
    php_handlebars_options_call(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}

PHP_METHOD(HandlebarsOptions, offsetExists)
{
    zval * _this_zval = getThis();
    zend_string * offset;
    zval * prop;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(offset)
    ZEND_PARSE_PARAMETERS_END();

    prop = zend_read_property_ex(Z_OBJCE_P(_this_zval), _this_zval, offset, 1, NULL);
    RETURN_BOOL(prop != NULL);
}

PHP_METHOD(HandlebarsOptions, offsetGet)
{
    zval * _this_zval = getThis();
    zend_string * offset;
    zval * prop;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(offset)
    ZEND_PARSE_PARAMETERS_END();

    prop = zend_read_property_ex(Z_OBJCE_P(_this_zval), _this_zval, offset, 1, NULL);
    RETURN_ZVAL(prop, 1, 0);
}

PHP_METHOD(HandlebarsOptions, offsetSet)
{
    zend_throw_exception(HandlebarsRuntimeException_ce_ptr, "offsetSet is not implemented", 0);
}

PHP_METHOD(HandlebarsOptions, offsetUnset)
{
    zend_throw_exception(HandlebarsRuntimeException_ce_ptr, "offsetUnset is not implemented", 0);
}

/* {{{ Handlebars\Options methods */
static zend_function_entry HandlebarsOptions_methods[] = {
    PHP_ME(HandlebarsOptions, __construct, HandlebarsOptions_construct_args, ZEND_ACC_PUBLIC)
    PHP_ME(HandlebarsOptions, fn, HandlebarsOptions_fn_args, ZEND_ACC_PUBLIC)
    PHP_ME(HandlebarsOptions, inverse, HandlebarsOptions_fn_args, ZEND_ACC_PUBLIC)
    PHP_ME(HandlebarsOptions, offsetExists, HandlebarsOptions_offsetExists_args, ZEND_ACC_PUBLIC)
    PHP_ME(HandlebarsOptions, offsetGet, HandlebarsOptions_offsetExists_args, ZEND_ACC_PUBLIC)
    PHP_ME(HandlebarsOptions, offsetSet, HandlebarsOptions_offsetSet_args, ZEND_ACC_PUBLIC)
    PHP_ME(HandlebarsOptions, offsetUnset, HandlebarsOptions_offsetExists_args, ZEND_ACC_PUBLIC)
    PHP_FE_END
};
/* }}} Handlebars\Options methods */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(handlebars_options)
{
    zend_class_entry ce;

    INTERNED_NAME = zend_new_interned_string(zend_string_init(ZEND_STRL("name"), 1));
    INTERNED_FN = zend_new_interned_string(zend_string_init(ZEND_STRL("fn"), 1));
    INTERNED_INVERSE = zend_new_interned_string(zend_string_init(ZEND_STRL("inverse"), 1));
    INTERNED_SCOPE = zend_new_interned_string(zend_string_init(ZEND_STRL("scope"), 1));
    INTERNED_HASH = zend_new_interned_string(zend_string_init(ZEND_STRL("hash"), 1));
    INTERNED_DATA = zend_new_interned_string(zend_string_init(ZEND_STRL("data"), 1));
    INTERNED_BLOCK_PARAMS = zend_new_interned_string(zend_string_init(ZEND_STRL("blockParams"), 1));

    memcpy(&HandlebarsOptions_obj_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    HandlebarsOptions_obj_handlers.offset = XtOffsetOf(struct php_handlebars_options_obj, std);
    HandlebarsOptions_obj_handlers.free_obj = php_handlebars_options_obj_free;
    HandlebarsOptions_obj_handlers.has_property = php_handlebars_options_object_has_property;
    HandlebarsOptions_obj_handlers.read_property = php_handlebars_options_object_read_property;
    //HandlebarsOptions_obj_handlers.get_properties = php_handlebars_options_object_get_properties;
    HandlebarsOptions_obj_handlers.clone_obj = NULL;

    INIT_CLASS_ENTRY(ce, "Handlebars\\Options", HandlebarsOptions_methods);
    HandlebarsOptions_ce_ptr = zend_register_internal_class(&ce);
    HandlebarsOptions_ce_ptr->create_object = php_handlebars_options_obj_create;
    zend_class_implements(HandlebarsOptions_ce_ptr, 1, zend_ce_arrayaccess);

    zend_hash_init(&HandlebarsOptions_prop_handlers, 0, NULL, NULL, 1);
    register_prop_handler("name", hbs_read_name);
    register_prop_handler("program", hbs_read_program);
    register_prop_handler("inverse", hbs_read_inverse);
    register_prop_handler("scope", hbs_read_scope);
    register_prop_handler("hash", hbs_read_hash);
    register_prop_handler("data", hbs_read_data);

    // Note: declaring these prevents dynamic initialization in PHP7
    /*
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("name"), ZEND_ACC_PUBLIC);
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("hash"), ZEND_ACC_PUBLIC);
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("fn"), ZEND_ACC_PUBLIC);
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("inverse"), ZEND_ACC_PUBLIC);
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("scope"), ZEND_ACC_PUBLIC);
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("data"), ZEND_ACC_PUBLIC);
    */

    // Used by handlebars.php
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("ids"), ZEND_ACC_PUBLIC);
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("hashIds"), ZEND_ACC_PUBLIC);
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("hashTypes"), ZEND_ACC_PUBLIC);
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("hashContexts"), ZEND_ACC_PUBLIC);
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("types"), ZEND_ACC_PUBLIC);
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("contexts"), ZEND_ACC_PUBLIC);
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("args"), ZEND_ACC_PUBLIC);
    zend_declare_property_null(HandlebarsOptions_ce_ptr, ZEND_STRL("partial"), ZEND_ACC_PUBLIC);

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MSHUTDOWN_FUNCTION(handlebars_options)
{
    zend_hash_destroy(&HandlebarsOptions_prop_handlers);

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
