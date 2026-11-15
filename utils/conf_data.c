#include "putty.h"

#define CONF_ENUM_BEGIN(name)                                           \
    static const ConfSaveEnumValue conf_enum_values_##name[] = {
#define CONF_ENUM_END(name)                                             \
    }; const ConfSaveEnumType conf_enum_##name = {                      \
        conf_enum_values_##name,              /* values */              \
        lenof(conf_enum_values_##name),       /* nvalues */             \
    };

#define VALUE(eval, sval) { eval, sval, false },
#define VALUE_OBSOLETE(eval, sval) { eval, sval, true },

#include "conf-enums.h"

bool conf_enum_map_to_storage(const ConfSaveEnumType *etype,
                              int confval, int *storageval_out)
{
    size_t i;
    for (i = 0; i < etype->nvalues; i++)
        if (!etype->values[i].obsolete &&
            etype->values[i].confval == confval) {
            *storageval_out = etype->values[i].storageval;
            return true;
        }
    return false;
}

bool conf_enum_map_from_storage(const ConfSaveEnumType *etype,
                                int storageval, int *confval_out)
{
    size_t i;
    for (i = 0; i < etype->nvalues; i++)
        if (etype->values[i].storageval == storageval) {
            *confval_out = etype->values[i].confval;
            return true;
        }
    return false;
}

/* Fields of ConfKeyInfo, in order: subkey_type, value_type,
 * default_value (bval, ival, sval), save_custom, load_custom,
 * not_saved, save_keyword, storage_enum */
#define CONF_OPTION(id, vtype, stype, def, kw, senum, flags) \
    { stype, vtype, { def }, flags, kw, senum },
#define VALUE_TYPE(x) CONF_TYPE_ ## x
#define SUBKEY_TYPE(x) CONF_TYPE_ ## x
#define NO_SUBKEY CONF_TYPE_NONE
#define DEFAULT_INT(x) false, x, NULL
#define DEFAULT_STR(x) false, 0, x
#define DEFAULT_BOOL(x) x, 0, NULL
#define NO_DEFAULT false, 0, NULL
#define SAVE_KEYWORD(x) x
#define NO_KEYWORD NULL
#define STORAGE_ENUM(x) &conf_enum_ ## x
#define NO_STORAGE_ENUM NULL
#define NORMAL_SAVE false, false, false
#define LOAD_CUSTOM false, true, false
#define SAVE_CUSTOM true, false, false
#define LOAD_SAVE_CUSTOM true, true, false
#define NOT_SAVED false, false, true

const ConfKeyInfo conf_key_info[] = {
    #include "conf.h"
};
