#include "abi.h"

#include <parser/symbols.h>
#include <codegen/codegen.h>
#include <codegen/registers.h>
#include <arch/calling.h>
#include <common.h>

static const int calling_convention[] = { REG_RCX, REG_RDX, REG_R8, REG_R9 };
//static const int return_convention[] = { REG_RAX };
static const int shadow_space = 32;

struct ms_data {
	int n_args;

	int is_variadic;

	int returns_address;
	var_id ret_address;
};

static int fits_into_reg(struct type *type) {
	int size = calculate_size(type);
	return size == 1 || size == 2 || size == 4 || size == 8;
}

static void ms_ir_function_call(var_id result, var_id func_var, struct type *type, int n_args, struct type **argument_types, var_id *args) {
	struct type *return_type = type->children[0];

	var_id registers[4];
	int register_idx = 0;
	int ret_in_register = 0;

	if (!type_is_simple(return_type, ST_VOID)) {
		if (fits_into_reg(return_type)) {
			ret_in_register = 1;
		} else {
			registers[0] = new_variable_sz(8, 1, 0);
			IR_PUSH_ADDRESS_OF(registers[0], result);

			register_idx++;
		}
	}

	int stack_sub = MAX(32, round_up_to_nearest(n_args * 8, 16));

	IR_PUSH_MODIFY_STACK_POINTER(-stack_sub - shadow_space);
	int current_mem = 0;
	for (int i = 0; i < n_args; i++) {
		var_id reg_to_push = args[i];

		if (!fits_into_reg(argument_types[i])) {
			reg_to_push = new_variable_sz(8, 1, 1);
			IR_PUSH_ADDRESS_OF(reg_to_push, args[i]);
		}

		if (register_idx < 4) {
			registers[register_idx++] = reg_to_push;
		} else {
			IR_PUSH_STORE_STACK_RELATIVE(current_mem + shadow_space, reg_to_push);
			current_mem += 8;
		}
	}

	for (int i = 0; i < register_idx; i++)
		IR_PUSH_SET_REG(registers[i], calling_convention[i], 0);

	IR_PUSH_CALL(func_var, REG_RBX);

	if (ret_in_register)
		IR_PUSH_GET_REG(result, REG_RAX, 0);

	IR_PUSH_MODIFY_STACK_POINTER(+stack_sub + shadow_space);
}

static void ms_ir_function_new(struct type *type, var_id *args, const char *name, int is_global) {
	struct type *return_type = type->children[0];
	int n_args = type->n - 1;

	struct function *func = &ADD_ELEMENT(ir.size, ir.cap, ir.functions);
	*func = (struct function) {
		.name = name,
		.is_global = is_global,
	};

	struct ms_data abi_data = { 0 };

	ir_block_start(new_block());

	int register_idx = 0;
	if (!type_is_simple(return_type, ST_VOID) && !fits_into_reg(return_type)) {
		abi_data.returns_address = 1;
		abi_data.ret_address = new_variable_sz(8, 1, 0);

		register_idx++;
	}

	if (type->function.is_variadic) {
		abi_data.is_variadic = 1;
		abi_data.n_args = n_args;
	}

	static int loads_cap = 0;
	struct load_pair {
		var_id from, to;
	};
	int loads_size = 0;
	struct load_pair *loads = NULL;
	
	int current_mem = 0;

	for (int i = 0; i < n_args; i++) {
		var_id reg_to_push = args[i];

		if (!fits_into_reg(type->children[i + 1])) {
			reg_to_push = new_variable_sz(8, 1, 1);

			ADD_ELEMENT(loads_size, loads_cap, loads) = (struct load_pair) {
				reg_to_push, args[i]
			};
		}

		if (register_idx < 4) {
			IR_PUSH_GET_REG(reg_to_push, calling_convention[register_idx++], 0);
		} else {
			IR_PUSH_LOAD_BASE_RELATIVE(reg_to_push, current_mem + 16 + shadow_space);
			current_mem += 8;
		}
	}

	for (int i = 0; i < loads_size; i++) {
		IR_PUSH_LOAD(loads[i].to, loads[i].from);
	}

	func->abi_data = malloc(sizeof (struct ms_data));
	*(struct ms_data *)func->abi_data = abi_data;
}

static void ms_ir_function_return(struct function *func, var_id value, struct type *type) {
	(void)func, (void)value;
	if (type == type_simple(ST_VOID))
		return;

	struct ms_data *abi_data = func->abi_data;

	if (abi_data->returns_address) {
		IR_PUSH_STORE(value, abi_data->ret_address);
	} else {
		IR_PUSH_SET_REG(value, 0, 0);
	}
}

static void ms_emit_function_preamble(struct function *func) {
	struct ms_data *abi_data = func->abi_data;

	if (!abi_data->is_variadic)
		return;

	emit("movq %%rcx, 16(%%rbp)");
	emit("movq %%rdx, 24(%%rbp)");
	emit("movq %%r8, 32(%%rbp)");
	emit("movq %%r9, 40(%%rbp)");
}

static void ms_emit_va_start(var_id result, struct function *func) {
	struct ms_data *abi_data = func->abi_data;

	emit("leaq %d(%%rbp), %%rax", abi_data->n_args * 8 + 16);
	scalar_to_reg(result, REG_RDX);
	emit("movq %%rax, (%%rdx)");
}

static void ms_emit_va_arg(var_id result, var_id va_list, struct type *type) {
	(void)result, (void)va_list, (void)type;

	scalar_to_reg(va_list, REG_RBX); // va_list is a pointer to the actual va_list.
	emit("movq (%%rbx), %%rax");
	emit("leaq 8(%rax), %rdx");
	emit("movq %%rdx, (%%rbx)");
	emit("movq (%rax), %rax");
	if (fits_into_reg(type)) {
		reg_to_scalar(REG_RAX, result);
	} else {
		emit("movq %%rax, %%rdi");
		emit("leaq -%d(%%rbp), %rsi", variable_info[result].stack_location);
		codegen_memcpy(get_variable_size(result));
	}
}

void abi_init_microsoft(void) {
	abi_ir_function_call = ms_ir_function_call;
	abi_ir_function_new = ms_ir_function_new;
	abi_ir_function_return = ms_ir_function_return;
	abi_emit_function_preamble = ms_emit_function_preamble;
	abi_emit_va_start = ms_emit_va_start;
	abi_emit_va_start = ms_emit_va_start;
	abi_emit_va_arg = ms_emit_va_arg;

	// Initialize the __builtin_va_list typedef.
	struct symbol_typedef *sym =
		symbols_add_typedef(sv_from_str("__builtin_va_list"));

	sym->data_type = type_pointer(type_simple(ST_VOID));
}
