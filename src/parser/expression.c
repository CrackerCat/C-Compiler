#include "expression.h"
#include <common.h>
#include "declaration.h"
#include <assert.h>
#include "symbols.h"
#include <codegen/rodata.h>
#include <precedence.h>

void convert_arithmetic(struct expr *a,
						struct expr *b,
						struct expr **converted_a,
						struct expr **converted_b) {
	struct type *a_type = a->data_type,
		*b_type = b->data_type;

	if (a_type->type != TY_SIMPLE ||
		b_type->type != TY_SIMPLE) {

		*converted_a = a;
		*converted_b = b;

		return;
	}
	assert(a_type->type == TY_SIMPLE);
	assert(b_type->type == TY_SIMPLE);

	enum simple_type a_stype = a_type->simple,
		b_stype = b_type->simple;

	enum simple_type target_type = 0;

	// 6.3.1.8
	if (a_stype == ST_LDOUBLE || b_stype == ST_LDOUBLE)
		target_type = ST_LDOUBLE;

    else if (a_stype == ST_DOUBLE || b_stype == ST_DOUBLE)
		target_type = ST_DOUBLE;

	else if (a_stype == ST_FLOAT || b_stype == ST_FLOAT)
		target_type = ST_FLOAT;

	else if (a_stype == b_stype) {
		target_type = a_stype;

	} else if (is_signed(a_stype) == is_signed(b_stype)) {
		if (type_rank(a_stype) > type_rank(b_stype))
			target_type = a_stype;
		else
			target_type = b_stype;

	} else if (!is_signed(a_stype) &&
			   type_rank(a_stype) >= type_rank(b_stype)) {
		target_type = a_stype;
	} else if (!is_signed(b_stype) &&
			   type_rank(b_stype) >= type_rank(a_stype)) {
		target_type = b_stype;

	} else if (is_signed(a_stype) &&
		is_contained_in(a_stype, b_stype)) {
		target_type = a_stype;
	} else if (is_signed(b_stype) &&
		is_contained_in(b_stype, a_stype)) {
		target_type = b_stype;

	} else if (is_signed(b_stype)) {
		target_type = to_unsigned(b_stype);

	} else if (is_signed(a_stype)) {
		target_type = to_unsigned(a_stype);
	} else {
		ERROR("Internal compiler error!");
	}

	*converted_a = expression_cast(a, type_simple(target_type));
	*converted_b = expression_cast(b, type_simple(target_type));
}

struct type *calculate_type(struct expr *expr) {
	switch (expr->type) {
	case E_CONSTANT:
		assert(expr->constant.type == CONSTANT_TYPE);
		return expr->constant.data_type;

	case E_BINARY_OP:
		return operators_get_result_type(expr->binary_op.op, expr->args[0]->data_type,
										 expr->args[1]->data_type);

	case E_UNARY_OP:
		return expr->args[0]->data_type;

	case E_SYMBOL:
		return expr->symbol.type;

	case E_CALL: {
		struct type *callee_type = expr->call.callee->data_type;
		if (callee_type->type == TY_FUNCTION)
			return callee_type->children[0];
		else if (callee_type->type == TY_POINTER)
			return type_deref(callee_type)->children[0];
		else {
			printf("POS: ");
			PRINT_POS(expr->pos);
			printf("\n");
			ERROR("Can't call type %s\n", type_to_string(callee_type));
		}
	}

	case E_STRING_LITERAL:
		return type_pointer(type_simple(ST_CHAR));

	case E_VARIABLE:
		return get_variable_type(expr->variable.id);

	case E_POINTER_ADD:
		return expr->args[0]->data_type;

	case E_INDIRECTION:
		return type_deref(expr->args[0]->data_type);

	case E_ADDRESS_OF:
		return type_pointer(expr->args[0]->data_type);

	case E_ARRAY_PTR_DECAY:
		return type_pointer(expr->args[0]->data_type->children[0]);

	case E_POSTFIX_INC:
	case E_POSTFIX_DEC:
		return expr->args[0]->data_type;

	case E_ASSIGNMENT:
	case E_ASSIGNMENT_OP:
	case E_ASSIGNMENT_POINTER_ADD:
		return expr->args[0]->data_type;

	case E_CAST:
		return expr->cast.target;

	case E_DOT_OPERATOR: {
		struct type *type;
		int offset;
		type_select(expr->member.lhs->data_type, expr->member.member_idx, &offset, &type);
		return type;
	}

	case E_CONDITIONAL:
		assert(expr->args[1]->data_type == expr->args[2]->data_type);
		return expr->args[1]->data_type;

	case E_BUILTIN_VA_ARG:
		return expr->va_arg_.t;
		
	case E_BUILTIN_VA_START:
	case E_BUILTIN_VA_END:
	case E_BUILTIN_VA_COPY:
		return type_simple(ST_VOID);

	case E_COMPOUND_LITERAL:
		return expr->compound_literal.type;

	case E_POINTER_DIFF:
		return type_simple(ST_INT);

	case E_COMMA:
		return expr->args[1]->data_type;

	default:
		printf("%d\n", expr->type);
		NOTIMP();
	}
}

int dont_decay_ptr[E_NUM_TYPES] = {
	[E_ADDRESS_OF] = 1,
	[E_ARRAY_PTR_DECAY] = 1,
};

int num_args[E_NUM_TYPES] = {
	[E_POSTFIX_INC] = 1,
	[E_POSTFIX_DEC] = 1,
	[E_ADDRESS_OF] = 1,
	[E_INDIRECTION] = 1,
	[E_UNARY_OP] = 1,
	[E_ALIGNOF] = 1,
	[E_BINARY_OP] = 2,
	[E_ASSIGNMENT] = 2,
	[E_CONDITIONAL] = 3,
	[E_COMMA] = 2,
	[E_ASSIGNMENT_OP] = 2,
};

int does_integer_conversion[E_NUM_TYPES] = {
	[E_UNARY_OP] = 1,
	[E_BINARY_OP] = 1,
	[E_CONDITIONAL] = 1,
};

int does_arithmetic_conversion[E_NUM_TYPES] = {
	[E_BINARY_OP] = 1,
	[E_ASSIGNMENT_OP] = 1,
};

struct expr *do_integer_promotion(struct expr *expr) {
	struct type *current_type = expr->data_type;
	
	if (current_type->type != TY_SIMPLE) {
		return expr;
	}

	enum simple_type simple_type = current_type->simple;

	switch(simple_type) {
	case ST_BOOL:
	case ST_CHAR:
	case ST_SCHAR:
	case ST_UCHAR:
	case ST_SHORT:
	case ST_USHORT:
		return expression_cast(expr, type_simple(ST_INT));
	default:
		return expr;
	}
}

void change_to_additive_pointer(struct expr *expr) {
	if (expr->type != E_BINARY_OP ||
		expr->binary_op.op != OP_ADD)
		return;

	int lhs_ptr = type_is_pointer(expr->args[0]->data_type),
		rhs_ptr = type_is_pointer(expr->args[1]->data_type);

	if (!(lhs_ptr || rhs_ptr))
		return;

	if (lhs_ptr && rhs_ptr) {
		ERROR("Invalid");
	}

	if (!lhs_ptr && rhs_ptr) {
		SWAP(struct expr *, expr->args[0], expr->args[1]);
	}

	// Left hand side is now a pointer.
	expr->type = E_POINTER_ADD;
}

void decay_array(struct expr **expr) {
	struct type *type = (*expr)->data_type;
	if (type->type == TY_ARRAY ||
		type->type == TY_INCOMPLETE_ARRAY ||
		type->type == TY_VARIABLE_LENGTH_ARRAY) {
		*expr = EXPR_ARGS(E_ARRAY_PTR_DECAY, *expr);
	} else if (type->type == TY_FUNCTION) {
		*expr = EXPR_ARGS(E_ADDRESS_OF, *expr);
	}
}

void cast_conditional(struct expr *expr) {
	if (expr->type != E_CONDITIONAL)
		return;

	struct type *a = expr->args[1]->data_type,
		*b = expr->args[2]->data_type;

	if (a == b)
		return;

	if (type_is_pointer(a) && type_is_pointer(b)) {
		if (type_deref(a) == type_simple(ST_VOID))
			expr->args[1] = expression_cast(expr->args[1], b);
		else if (type_deref(b) == type_simple(ST_VOID))
			expr->args[2] = expression_cast(expr->args[2], a);
		else {
			ERROR("Invalid combination of data types:\n%s and %s\n",
				  strdup(type_to_string(a)),
				  strdup(type_to_string(b)));
		}
	} else if (type_is_arithmetic(a) &&
			   type_is_arithmetic(b)) {
		convert_arithmetic(expr->args[1], expr->args[2],
						   &expr->args[1], &expr->args[2]);
	} else if (a != b) {
		ERROR("Invalid combination of data types:\n%s and %s\n",
			  strdup(type_to_string(a)),
			  strdup(type_to_string(b)));
	}
}

void fix_assignment_add(struct expr *expr) {
	if (expr->type != E_ASSIGNMENT_OP ||
		expr->binary_op.op != OP_ADD)
		return;

	if (type_is_pointer(expr->args[0]->data_type))
		expr->type = E_ASSIGNMENT_POINTER_ADD;
}

void fix_pointer_sub(struct expr *expr) {
	if (expr->type != E_BINARY_OP ||
		expr->binary_op.op != OP_SUB)
		return;

	struct type *lhs_type = expr->args[0]->data_type,
		*rhs_type = expr->args[1]->data_type;

	if (type_is_pointer(lhs_type) &&
		type_is_pointer(rhs_type)) {
		assert(lhs_type == rhs_type);
		expr->type = E_POINTER_DIFF;
	} else if (type_is_pointer(lhs_type)) {
		NOTIMP();
	} else if (type_is_pointer(rhs_type)) {
		NOTIMP();
	} else {
		// Not any kind of pointer subtraction.
	}
}

void fix_pointer_op(struct expr *expr) {
	if (expr->type != E_BINARY_OP)
		return;


}

struct expr *expr_new(struct expr expr) {
	cast_conditional(&expr);

	for (int i = 0; i < num_args[expr.type]; i++) {
		if (!expr.args[i]) {
			PRINT_POS(T0->pos);
			ERROR("Wrongly constructed expression of type %d", expr.type);
		}
	}

	if (!dont_decay_ptr[expr.type]) {
		for (int i = 0; i < num_args[expr.type]; i++)
			decay_array(&expr.args[i]);

		if (expr.type == E_BUILTIN_VA_ARG) {
			decay_array(&expr.va_arg_.v);
		} else if (expr.type == E_BUILTIN_VA_START) {
			decay_array(&expr.va_start_.array);
		} else if (expr.type == E_BUILTIN_VA_COPY) {
			decay_array(&expr.va_copy_.d);
			decay_array(&expr.va_copy_.s);
		} else if (expr.type == E_CALL) {
			for (int i = 0; i < expr.call.n_args; i++) {
				decay_array(&expr.call.args[i]);
			}
		}
	}
	
	fix_assignment_add(&expr);
	fix_pointer_sub(&expr);
	change_to_additive_pointer(&expr);

	int integer_promotion = does_integer_conversion[expr.type];
	if (integer_promotion) {
		for (int i = 0; i < num_args[expr.type]; i++) {
			expr.args[i] = do_integer_promotion(expr.args[i]);
		}
	}

	if (does_arithmetic_conversion[expr.type]) {
		if (num_args[expr.type] != 2) {
			ERROR("Wrong number of arguments %d", expr.type);
		}

		convert_arithmetic(expr.args[0], expr.args[1],
						   &expr.args[0], &expr.args[1]);
	}

	expr.data_type = calculate_type(&expr);

	struct expr *ret = malloc(sizeof *ret);
	*ret = expr;

	return ret;
}

struct lvalue {
	enum {
		LVALUE_VARIABLE,
		LVALUE_PTR
	} type;

	var_id variable;
};

// Loads pointer into return value.
struct lvalue expression_to_lvalue(struct expr *expr) {
	switch (expr->type) {
	case E_INDIRECTION:
		return (struct lvalue) { LVALUE_PTR, expression_to_ir(expr->args[0]) };

	case E_VARIABLE:
		return (struct lvalue) { LVALUE_VARIABLE, expr->variable.id };

	case E_DOT_OPERATOR: {
		struct lvalue lvalue = expression_to_lvalue(expr->member.lhs);
		switch (lvalue.type) {
		case LVALUE_VARIABLE: {
			var_id address = new_variable(type_pointer(expr->member.lhs->data_type), 1);
			var_id member_address = new_variable(type_pointer(expr->data_type), 1);
			IR_PUSH_ADDRESS_OF(address, lvalue.variable);
			IR_PUSH_GET_MEMBER(member_address, address, expr->member.member_idx);
			return (struct lvalue) { LVALUE_PTR, member_address };
		} break;

		case LVALUE_PTR: {
			var_id member_address = new_variable(type_pointer(expr->data_type), 1);
			IR_PUSH_GET_MEMBER(member_address, lvalue.variable, expr->member.member_idx);
			return (struct lvalue) { LVALUE_PTR, member_address };
		} break;

		default:
			NOTIMP();
		}
	} break;

	case E_SYMBOL: {
		var_id ptr_result = new_variable(type_pointer(expr->data_type), 1);
		IR_PUSH_GET_SYMBOL_PTR(expr->symbol.name, ptr_result);
		return (struct lvalue) { LVALUE_PTR, ptr_result };
	} break;

	default:
		ERROR("NOt imp %d\n", expr->type);
		NOTIMP();
	}
}

struct type *get_lvalue_type(struct lvalue lvalue) {
	switch (lvalue.type) {
	case LVALUE_VARIABLE:
		return get_variable_type(lvalue.variable);

	case LVALUE_PTR:
		return type_deref(get_variable_type(lvalue.variable));

	default:
		NOTIMP();
	}
}

var_id lvalue_load(struct lvalue lvalue) {
	struct type *type = get_lvalue_type(lvalue);
	var_id ret = new_variable(type, 1);

	switch (lvalue.type) {
	case LVALUE_VARIABLE:
		IR_PUSH_COPY(ret, lvalue.variable);
		break;

	case LVALUE_PTR:
		IR_PUSH_LOAD(ret, lvalue.variable);
		break;

	default:
		NOTIMP();
	}

	return ret;
}

void lvalue_store(struct lvalue lvalue, var_id value) {
	switch (lvalue.type) {
	case LVALUE_VARIABLE:
		IR_PUSH_COPY(lvalue.variable, value);
		break;

	case LVALUE_PTR:
		IR_PUSH_STORE(value, lvalue.variable);
		break;

	default:
		NOTIMP();
	}
}

var_id expression_to_ir(struct expr *expr) {
	switch(expr->type) {
	case E_BINARY_OP: {
		var_id lhs = expression_to_ir(expr->args[0]);
		var_id rhs = expression_to_ir(expr->args[1]);
		var_id res = new_variable(expr->data_type, 1);

		IR_PUSH_BINARY_OPERATOR(expr->binary_op.op, lhs, rhs, res);
		return res;
	} break;

	case E_UNARY_OP:
	{
		var_id rhs = expression_to_ir(expr->args[0]);
		var_id res = new_variable(expr->data_type, 1);

		IR_PUSH_UNARY_OPERATOR(expr->unary_op, rhs, res);
		return res;
	} break;

	case E_CONSTANT: {
		var_id res = new_variable(expr->data_type, 1);
		IR_PUSH_CONSTANT(expr->constant, res);
		return res;
	} break;

	case E_STRING_LITERAL: {
		var_id res = new_variable(expr->data_type, 1);
		IR_PUSH_STRING_LITERAL(expr->string_literal, res);
		return res;
	} break;

	case E_CALL: {
		struct expr *callee = expr->call.callee;

		struct type *signature = NULL;
		switch (callee->type) {
		case E_SYMBOL:
			signature = callee->symbol.type;
			break;
		default:
			assert(type_is_pointer(callee->data_type));
			signature = type_deref(callee->data_type);
			break;
		}

		var_id *args = malloc(sizeof *args * expr->call.n_args);
		var_id res = new_variable(expr->data_type, 1);
		for (int i = 0; i < expr->call.n_args; i++) {
			if (i + 1 < signature->n) {
				args[i] = expression_to_ir(expression_cast(expr->call.args[i],
														   signature->children[i + 1]));
			} else {
				args[i] = expression_to_ir(expr->call.args[i]);
			}
		}

		switch (callee->type) {
		case E_SYMBOL: {
			struct type *func_type = callee->symbol.type;
			const char *func_name = callee->symbol.name;
			IR_PUSH_CALL_LABEL(func_name, func_type, expr->call.n_args, args, res);
			return res;
		} break;
		default: {
			var_id func_var = expression_to_ir(callee);
			struct type *func_type = get_variable_type(func_var);

			assert(type_is_pointer(func_type));

			IR_PUSH_CALL_VARIABLE(func_var, func_type, expr->call.n_args, args, res);
			return res;
		}
		}
	}

	case E_VARIABLE:
		return expr->variable.id;

	case E_INDIRECTION: {
		// Assume that this will be a rvalue.
		var_id res = new_variable(expr->data_type, 1);
		IR_PUSH_LOAD(res, expression_to_ir(expr->args[0]));
		return res;
	}

	case E_ADDRESS_OF: {
		struct lvalue lvalue = expression_to_lvalue(expr->args[0]);
		switch (lvalue.type) {
		case LVALUE_PTR:
			return lvalue.variable;
		case LVALUE_VARIABLE: {
			var_id res = new_variable(expr->data_type, 1);
			IR_PUSH_ADDRESS_OF(res, lvalue.variable);
			return res;
		}
		default:
			NOTIMP();
		}
	}

	case E_ARRAY_PTR_DECAY: {
		struct lvalue lvalue = expression_to_lvalue(expr->args[0]);
		switch (lvalue.type) {
		case LVALUE_PTR: {
			var_id res = new_variable(expr->data_type, 1);
			IR_PUSH_CAST(res, lvalue.variable, expr->data_type);
			return res;
		}
		case LVALUE_VARIABLE: {
			var_id array_ptr = new_variable(type_pointer(get_variable_type(lvalue.variable)), 1);
			var_id res = new_variable(expr->data_type, 1);
			IR_PUSH_ADDRESS_OF(array_ptr, lvalue.variable);
			IR_PUSH_CAST(res, array_ptr, expr->data_type);
			return res;
		}
		}
	} NOTIMP();

	case E_POINTER_ADD: {
		var_id res = new_variable(expr->data_type, 1);
		IR_PUSH_POINTER_INCREMENT(res, expression_to_ir(expr->args[0]),
								  expression_to_ir(expr->args[1]));
		return res;
	}

	case E_POINTER_DIFF: {
		// TODO: Make this work on variable length objects.
		var_id res = new_variable(expr->data_type, 1);
		IR_PUSH_POINTER_DIFF(res, expression_to_ir(expr->args[0]),
							 expression_to_ir(expr->args[1]));
		return res;
	}

	case E_POSTFIX_INC: {
		var_id res = new_variable(expr->data_type, 1);
		struct type *type = get_variable_type(res);

		struct lvalue lvalue = expression_to_lvalue(expr->args[0]);
		var_id value = lvalue_load(lvalue);
		IR_PUSH_COPY(res, value);

		if (type->type == TY_POINTER) {
			var_id constant_one = expression_to_ir(EXPR_INT(1));
			IR_PUSH_POINTER_INCREMENT(value, value, constant_one);
		} else {
			var_id constant_one = expression_to_ir(expression_cast(EXPR_INT(1), type));
			IR_PUSH_BINARY_OPERATOR(OP_ADD, value, constant_one, value);
		}
		lvalue_store(lvalue, value);
		return res;
	} break;

	case E_POSTFIX_DEC: {
		var_id res = new_variable(expr->data_type, 1);
		struct type *type = get_variable_type(res);
		var_id constant_one = new_variable(type_simple(ST_INT), 1);
		IR_PUSH_CONSTANT(((struct constant) {.type = CONSTANT_TYPE, .data_type = type_simple(ST_INT), .i = 1}), constant_one);
		struct lvalue lvalue = expression_to_lvalue(expr->args[0]);
		var_id value = lvalue_load(lvalue);
		IR_PUSH_COPY(res, value);
		if (type->type == TY_POINTER) {
			NOTIMP();
			// Should decrement.
			IR_PUSH_POINTER_INCREMENT(value, value, constant_one);
		} else {
			IR_PUSH_BINARY_OPERATOR(OP_SUB, value, constant_one, value);
		}
		lvalue_store(lvalue, value);
		return res;
	} break;

	case E_ASSIGNMENT: {
		struct lvalue lvalue = expression_to_lvalue(expr->args[0]);
		var_id rhs = expression_to_ir(expression_cast(expr->args[1], expr->args[0]->data_type));

		lvalue_store(lvalue, rhs);
		return rhs;
	};

	case E_ASSIGNMENT_OP:
	{
		struct expr *lhs = expr->args[0];
		if (lhs->type == E_CAST)
			NOTIMP();
		struct lvalue lvalue = expression_to_lvalue(expr->args[0]);
		var_id rhs = expression_to_ir(expr->args[1]);

		var_id prev_val = lvalue_load(lvalue);
		enum operator_type ot = expr->binary_op.op;

		if (type_is_pointer(expr->args[0]->data_type) ||
			type_is_pointer(expr->args[1]->data_type)) {
			PRINT_POS(T0->pos);
			ERROR("OP: %d\n", ot);
		}

		IR_PUSH_BINARY_OPERATOR(ot, prev_val, rhs, prev_val);

		lvalue_store(lvalue, prev_val);
		return prev_val;
	} break;

	case E_ASSIGNMENT_POINTER_ADD:
	{
		struct lvalue lvalue = expression_to_lvalue(expr->args[0]);
		var_id rhs = expression_to_ir(expr->args[1]);

		var_id prev_val = lvalue_load(lvalue);

		assert(type_is_pointer(get_variable_type(prev_val)));

		IR_PUSH_POINTER_INCREMENT(prev_val, prev_val, rhs);

		lvalue_store(lvalue, prev_val);
		return prev_val;
	} break;

	case E_CAST: {
		var_id res = new_variable(expr->data_type, 1);
		IR_PUSH_CAST(res, expression_to_ir(expr->cast.arg), expr->cast.target);
		return res;
	}

	case E_DOT_OPERATOR: {
		var_id lhs = expression_to_ir(expr->member.lhs);
		var_id address = new_variable(type_pointer(expr->member.lhs->data_type), 1);
		var_id member_address = new_variable(type_pointer(expr->data_type), 1);
		IR_PUSH_ADDRESS_OF(address, lhs);
		IR_PUSH_GET_MEMBER(member_address, address, expr->member.member_idx);
		var_id res = new_variable(expr->data_type, 1);
		IR_PUSH_LOAD(res, member_address);
		return res;
	} break;

	case E_CONDITIONAL: {
		var_id condition = expression_to_ir(expr->args[0]);
		var_id res = new_variable(expr->data_type, 1);

		block_id block_true = new_block(),
			block_false = new_block(),
			block_end = new_block();

		IR_PUSH(.type = IR_IF_SELECTION,
				.if_selection = { condition, block_true, block_false });

		IR_PUSH_START_BLOCK(block_true);
		var_id true_val = expression_to_ir(expr->args[1]);
		IR_PUSH_COPY(res, true_val);
		IR_PUSH_GOTO(block_end);

		IR_PUSH_START_BLOCK(block_false);
		var_id false_val = expression_to_ir(expr->args[2]);
		IR_PUSH_COPY(res, false_val);
		IR_PUSH_GOTO(block_end);

		IR_PUSH_START_BLOCK(block_end);
		return res;
	} break;

	case E_BUILTIN_VA_END:
		// Null operation.
		return VOID_VAR;
		break;

	case E_BUILTIN_VA_START: {
		var_id ptr = expression_to_ir(expr->va_start_.array);
		IR_PUSH_VA_START(ptr);
		return VOID_VAR;
	} break;

	case E_BUILTIN_VA_ARG: {
		var_id res = new_variable(expr->data_type, 1);
		var_id ptr = expression_to_ir(expr->va_arg_.v);
		IR_PUSH_VA_ARG(ptr, res, expr->va_arg_.t);
		return res;
	} break;

	case E_BUILTIN_VA_COPY: {
		var_id dest = expression_to_ir(expr->va_copy_.d);
		var_id source = expression_to_ir(expr->va_copy_.s);

		var_id tmp = new_variable(type_deref(get_variable_type(dest)), 1);

		IR_PUSH_LOAD(tmp, source);
		IR_PUSH_STORE(tmp, dest);

		return VOID_VAR;
	}

	case E_COMPOUND_LITERAL: {
		struct initializer *init = expr->compound_literal.init;
		var_id variable = new_variable(expr->compound_literal.type, 1);
		IR_PUSH_SET_ZERO(variable);

		for (int i = 0; i < init->n; i++) {
			IR_PUSH_ASSIGN_CONSTANT_OFFSET(variable, expression_to_ir(init->pairs[i].expr), init->pairs[i].offset);
		}

		return variable;
	} break;

	case E_SYMBOL: {
		var_id res = new_variable(expr->data_type, 1);
		var_id ptr = new_variable(type_pointer(expr->symbol.type), 1);
		IR_PUSH_GET_SYMBOL_PTR(expr->symbol.name, ptr);
		IR_PUSH_LOAD(res, ptr);
		return res;
	} break;

	case E_COMMA: {
		expression_to_ir(expr->args[0]);
		return expression_to_ir(expr->args[1]);
	} break;

	default:
		printf("%d\n", expr->type);
		NOTIMP();
	}
}

// Parsing.

struct expr *parse_builtins(void) {
	if (TACCEPT(T_KVA_START)) {
		TEXPECT(T_LPAR);
		struct expr *v = parse_assignment_expression();
		TEXPECT(T_COMMA);
		struct expr *l = parse_assignment_expression();
		TEXPECT(T_RPAR);

		return expr_new((struct expr) {
				.type = E_BUILTIN_VA_START,
				.va_start_ = {v, l}
			});
	} else if (TACCEPT(T_KVA_COPY)) {
		TEXPECT(T_LPAR);
		struct expr *d = parse_assignment_expression();
		TEXPECT(T_COMMA);
		struct expr *s = parse_assignment_expression();
		TEXPECT(T_RPAR);
		return expr_new((struct expr) {
				.type = E_BUILTIN_VA_COPY,
				.va_copy_ = {d, s}
			});
	} else if (TACCEPT(T_KVA_END)) {
		TEXPECT(T_LPAR);
		struct expr *v = parse_assignment_expression();
		TEXPECT(T_RPAR);
		return expr_new((struct expr) {
				.type = E_BUILTIN_VA_END,
				.va_end_ = {v}
			});
	} else if (TACCEPT(T_KVA_ARG)) {
		TEXPECT(T_LPAR);
		struct expr *v = parse_assignment_expression();
		TEXPECT(T_COMMA);
		struct type *t = parse_type_name();
		if (!t)
			ERROR("Expected typename in var_arg");
		TEXPECT(T_RPAR);
		return expr_new((struct expr) {
				.type = E_BUILTIN_VA_ARG,
				.va_arg_ = {v, t}
			});
	} else {
		return NULL;
	}
}

struct expr *parse_primary_expression(int starts_with_lpar) {
	if (starts_with_lpar || TACCEPT(T_LPAR)) {
		struct expr *expr = parse_expression();
		TEXPECT(T_RPAR);
		return expr;
	} else if (T_ISNEXT(T_IDENT)) {
		struct symbol_identifier *sym = symbols_get_identifier(T0->str);

		if (!sym) {
			PRINT_POS(T0->pos);
			ERROR("Could not find identifier %s", T0->str);
		}

		switch (sym->type) {
		case IDENT_FUNCTION:
			TNEXT();
			return expr_new((struct expr) {
					.type = E_SYMBOL,
					.symbol = { sym->function.name, sym->function.type }
				});
			break;

		case IDENT_VARIABLE:
			TNEXT();
			return expr_new((struct expr) {
					.type = E_VARIABLE,
					.variable = { sym->variable }
				});
			break;

		case IDENT_CONSTANT:
			TNEXT();
			return expr_new((struct expr) {
					.type = E_CONSTANT,
					.constant = sym->constant
				});

		case IDENT_GLOBAL_VAR:
			TNEXT();
			return expr_new((struct expr) {
					.type = E_SYMBOL,
					.symbol = { sym->function.name, sym->function.type }
				});
			break;

		default:
			printf("%s\n", T0->str);
			NOTIMP();
		}

		NOTIMP();
	} else if (T_ISNEXT(T_NUM)) {
		struct constant c = constant_from_string(T0->str);
		TNEXT();
		return expr_new((struct expr) {
				.type = E_CONSTANT,
				.constant = c
			});
	} else if (T_ISNEXT(T_STRING)) {
		const char *str = T0->str;
		TNEXT();
		return expr_new((struct expr) {
				.type = E_STRING_LITERAL,
				.string_literal = str
			});
	} else if (T_ISNEXT(T_CHARACTER_CONSTANT)) {
		const char *str = T0->str;
		TNEXT();
		return EXPR_INT(escaped_to_str(str));
	} else if (T_ISNEXT(T_CHAR)) {
		NOTIMP();
	} else if (TACCEPT(T_KGENERIC)) {
		NOTIMP();
	} else {

	}
	return parse_builtins();
}

struct expr *parse_postfix_expression(int starts_with_lpar, struct expr *starting_lhs) {
	struct expr *lhs = starting_lhs ? starting_lhs : parse_primary_expression(starts_with_lpar);

	do {
		if (TACCEPT(T_LBRACK)) {
			struct expr *index = parse_expression();
			TEXPECT(T_RBRACK);
			lhs = expr_new((struct expr) {
					.type = E_INDIRECTION,
					.args = {
						EXPR_BINARY_OP(OP_ADD, lhs, index)
					}
				});
		} else if (TACCEPT(T_LPAR)) {
			struct position position = T0->pos;
			struct expr *buffer[128];

			int pos = 0;

			for (; pos < 128; pos++) {
				if (TACCEPT(T_RPAR))
					break;

				if (pos != 0)
					TEXPECT(T_COMMA);

				struct expr *expr = parse_assignment_expression();

				if (!expr)
					ERROR("Expected expression");

				buffer[pos] = expr;
			}

			if (pos == 128)
				NOTIMP();

			struct expr **args = malloc(sizeof *args * pos);
			memcpy(args, buffer, sizeof *args * pos);

			lhs = expr_new((struct expr) {
					.type = E_CALL,
					.call = { lhs, pos, args },
					.pos = position
				});
		} else if (TACCEPT(T_DOT)) {
			const char *identifier = T0->str;
			TNEXT();
			struct type *lhs_type = lhs->data_type;
			int idx = type_member_idx(lhs_type, identifier);

			lhs = expr_new((struct expr) {
					.type = E_DOT_OPERATOR,
					.member = { lhs, idx }
				});
		} else if (TACCEPT(T_ARROW)) {
			const char *identifier = T0->str;
			TNEXT();
			struct type *lhs_type = type_deref(lhs->data_type);
			int idx = type_member_idx(lhs_type, identifier);

			lhs = expr_new((struct expr) {
					.type = E_DOT_OPERATOR,
					.member = {
						EXPR_ARGS(E_INDIRECTION, lhs), idx
					}
				});
		} else if (TACCEPT(T_INC)) {
			lhs = expr_new((struct expr) {
					.type = E_POSTFIX_INC,
					.args = { lhs }
				});
		} else if (TACCEPT(T_DEC)) {
			lhs = expr_new((struct expr) {
					.type = E_POSTFIX_DEC,
					.args = { lhs }
				});
		} else {
			break;
		}
	} while (1);

	return lhs;
}

struct expr *parse_cast_expression(void);
struct expr *parse_unary_expression() {
	if (TACCEPT(T_INC)) {
		return EXPR_ASSIGNMENT_OP(OP_ADD, parse_unary_expression(), EXPR_INT(1));
	} else if (TACCEPT(T_DEC)) {
		return EXPR_ASSIGNMENT_OP(OP_SUB, parse_unary_expression(), EXPR_INT(1));
	} else if (TACCEPT(T_STAR)) {
		struct expr *rhs = parse_cast_expression();
		return expr_new((struct expr) {
				.type = E_INDIRECTION,
				.args = { rhs }
			});
	} else if (TACCEPT(T_AMP)) {
		struct expr *rhs = parse_cast_expression();
		return expr_new((struct expr) {
				.type = E_ADDRESS_OF,
				.args = { rhs }
			});
	} else if (TACCEPT(T_ADD)) {
		return EXPR_UNARY_OP(UOP_PLUS, parse_cast_expression());
	} else if (TACCEPT(T_SUB)) {
		return EXPR_UNARY_OP(UOP_NEG, parse_cast_expression());
	} else if (TACCEPT(T_BNOT)) {
		return EXPR_UNARY_OP(UOP_BNOT, parse_cast_expression());
	} else if (TACCEPT(T_NOT)) {
		struct expr *rhs = parse_cast_expression();
		return EXPR_ARGS(E_CONDITIONAL, rhs,
						 EXPR_INT(0),
						 EXPR_INT(1));
	} else if (TACCEPT(T_KSIZEOF)) {
		int size = 0;
		if (TACCEPT(T_LPAR)) {
			struct type *type = parse_type_name();
			if (type) {
				size = calculate_size(type);
				TEXPECT(T_RPAR);
			} else {
				struct expr *rhs = parse_expression();
				if (rhs->type == E_STRING_LITERAL) {
					size = strlen(rhs->string_literal) + 1;
				} else {
					size = calculate_size(rhs->data_type);
				}
				TEXPECT(T_RPAR);
			}
		} else {
			struct expr *rhs = parse_unary_expression();
			size = calculate_size(rhs->data_type);
		}
		// TODO: Size should perhaps not be an integer.
		struct constant c = {.type = CONSTANT_TYPE, .data_type = type_simple(ST_INT), .i = size };

		return expr_new((struct expr) {
				.type = E_CONSTANT,
				.constant = c
			});
	} else if (TACCEPT(T_KALIGNOF)) {
		NOTIMP();
	} else {
		return parse_postfix_expression(0, NULL);
	}
}

struct expr *parse_paren_or_cast_expression() {
	if (!TACCEPT(T_LPAR))
		return NULL;

	struct type *cast_type = parse_type_name();
	if (cast_type) {
		TEXPECT(T_RPAR);
		if (T0->type == T_LBRACE) {
			struct initializer *init = parse_initializer(&cast_type);

			struct expr *compound = expr_new((struct expr) {
					.type = E_COMPOUND_LITERAL,
					.compound_literal = { cast_type, init }
				});

			return parse_postfix_expression(0, compound);
		} else {
			struct expr *rhs = parse_cast_expression();
			if (!rhs)
				ERROR("Expected expression");
			return expression_cast(rhs, cast_type);
		}
	} else {
		// This is wrong.
		return parse_postfix_expression(1, NULL);
	}
}

struct expr *parse_cast_expression(void) {
	struct expr *ret = parse_paren_or_cast_expression();
	if (ret)
		return ret;

	return parse_unary_expression();
}

struct expr *parse_pratt(int precedence) {
	struct expr *lhs = parse_cast_expression();

	if (!lhs)
		return NULL;

	while (precedence < precedence_get(T0->type, PREC_INFIX, 1)) {
		int new_prec = precedence_get(T0->type, PREC_INFIX, 0);

		static int ops[T_COUNT][2] = {
			[T_COMMA] = {1, E_COMMA},
			[T_A] = {1, E_ASSIGNMENT},

			[T_MULA] = {2, OP_MUL},
			[T_DIVA] = {2, OP_DIV},
			[T_MODA] = {2, OP_MOD},
			[T_ADDA] = {2, OP_ADD},
			[T_SUBA] = {2, OP_SUB},
			[T_LSHIFTA] = {2, OP_LSHIFT},
			[T_RSHIFTA] = {2, OP_RSHIFT},
			[T_BANDA] = {2, OP_BAND},
			[T_XORA] = {2, OP_BXOR},
			[T_BORA] = {2, OP_BOR},

			[T_BOR] = {3, OP_BOR},
			[T_XOR] = {3, OP_BXOR},
			[T_AMP] = {3, OP_BAND},
			[T_EQ] = {3, OP_EQUAL},
			[T_NEQ] = {3, OP_NOT_EQUAL},
			[T_LEQ] = {3, OP_LESS_EQ},
			[T_GEQ] = {3, OP_GREATER_EQ},
			[T_L] = {3, OP_LESS},
			[T_G] = {3, OP_GREATER},
			[T_LSHIFT] = {3, OP_LSHIFT},
			[T_RSHIFT] = {3, OP_RSHIFT},
			[T_ADD] = {3, OP_ADD},
			[T_SUB] = {3, OP_SUB},
			[T_STAR] = {3, OP_MUL},
			[T_DIV] = {3, OP_DIV},
			[T_MOD] = {3, OP_MOD},
		};

		int tok_type = T0->type;
		if (ops[tok_type][0] == 1) {
			TNEXT();
			lhs = EXPR_ARGS(ops[tok_type][1], lhs, parse_pratt(new_prec));
		} else if (ops[tok_type][0] == 2) {
			TNEXT();
			lhs = EXPR_ASSIGNMENT_OP(ops[tok_type][1], lhs, parse_pratt(new_prec));
		} else if (ops[tok_type][0] == 3) {
			TNEXT();
			lhs = EXPR_BINARY_OP(ops[tok_type][1], lhs, parse_pratt(new_prec));
		} else if (TACCEPT(T_QUEST)) {
			struct expr *mid = parse_expression();
			TEXPECT(T_COLON);
			struct expr *rhs = parse_pratt(new_prec);
			lhs = EXPR_ARGS(E_CONDITIONAL, lhs, mid, rhs);
		} else if (TACCEPT(T_OR)) {
			// A || B -> A ? 1 : (B ? 1 : 0)
			struct expr *rhs = parse_pratt(new_prec);
			lhs = EXPR_ARGS(E_CONDITIONAL, lhs, EXPR_INT(1),
							EXPR_ARGS(E_CONDITIONAL, rhs, EXPR_INT(1), EXPR_INT(0)));
		} else if (TACCEPT(T_AND)) {
			// A && B -> A ? (B ? 1 : 0) : 0
			struct expr *rhs = parse_pratt(new_prec);
			lhs = EXPR_ARGS(E_CONDITIONAL, lhs,
							EXPR_ARGS(E_CONDITIONAL, rhs, EXPR_INT(1), EXPR_INT(0)),
							EXPR_INT(0));
		}
	}

	return lhs;
}

struct expr *parse_assignment_expression() {
	return parse_pratt(5);
}

struct expr *parse_expression() {
	return parse_pratt(0);
}

// Constant expressions.

int evaluate_constant_expression(struct expr *expr,
								 struct constant *constant) {
	switch (expr->type) {
	case E_CONSTANT:
		*constant = expr->constant;
		return 1;

	case E_ENUM_TO_INT:
		NOTIMP();
		return 1;

	case E_CAST: {
		struct constant rhs;
		if (!evaluate_constant_expression(expr->cast.arg, &rhs))
			return 0;
		*constant = constant_cast(rhs, expr->cast.target);
	} break;

	case E_VARIABLE:
	case E_DOT_OPERATOR:
	case E_CALL:
		return 0;

	case E_BINARY_OP: {
		struct constant lhs, rhs;
		if (!evaluate_constant_expression(expr->args[0], &lhs))
			return 0;
		if (!evaluate_constant_expression(expr->args[1], &rhs))
			return 0;
		*constant = operators_constant(expr->binary_op.op, lhs, rhs);
	} break;

	case E_STRING_LITERAL: {
		*constant = (struct constant) {
			.type = CONSTANT_LABEL,
			.data_type = type_pointer(type_simple(ST_CHAR)),
			.label = register_string(expr->string_literal)
		};
	} break;

	default:
		ERROR("Not imp %d\n", expr->type);
		NOTIMP();
	}
	return 1;
}

struct expr *expression_cast(struct expr *expr, struct type *type) {
	return (expr->data_type == type) ? expr :
		expr_new((struct expr) {
				.type = E_CAST,
				.cast = {expr, type}				   
			});
}
