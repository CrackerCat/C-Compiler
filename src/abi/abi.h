#ifndef ABI_H
#define ABI_H

#include <ir/variables.h>
#include <ir/ir.h>

void abi_init_sysv(void);
void abi_init_microsoft(void);

extern void (*abi_ir_function_call)(var_id result, var_id func_var, struct type *function_type, int n_args, struct type **argument_types, var_id *args);
extern void (*abi_ir_function_new)(struct type *type, var_id *args, const char *name, int is_global);
extern void (*abi_ir_function_return)(struct function *func, var_id value, struct type *type);

extern void (*abi_emit_function_preamble)(struct function *func);
extern void (*abi_emit_va_start)(var_id result, struct function *func);
extern void (*abi_emit_va_arg)(var_id result, var_id va_list, struct type *type);

#endif
