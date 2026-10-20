TESTS=	00_assignment.test \
	01_comment.test \
	02_printf.test \
	03_struct.test \
	04_for.test \
	05_array.test \
	06_case.test \
	07_function.test \
	08_while.test \
	09_do_while.test \
	10_pointer.test \
	11_precedence.test \
	12_hashdefine.test \
	13_integer_literals.test \
	14_if.test \
	15_recursion.test \
	16_nesting.test \
	17_enum.test \
	18_include.test \
	19_pointer_arithmetic.test \
	20_pointer_comparison.test \
	21_char_array.test \
	22_floating_point.test \
	23_type_coercion.test \
	24_math_library.test \
	25_quicksort.test \
	26_character_constants.test \
	28_strings.test \
	29_array_address.test \
	30_hanoi.test \
	31_args.test \
	32_led.test \
	33_ternary_op.test \
	34_array_assignment.test \
	35_sizeof.test \
	36_array_initializers.test \
	37_sprintf.test \
	38_multiple_array_index.test \
	39_typedef.test \
	40_stdio.test \
	41_hashif.test \
	43_void_param.test \
	44_scoped_declarations.test \
	45_empty_for.test \
	47_switch_return.test \
	48_nested_break.test \
	49_bracket_evaluation.test \
	50_logical_second_arg.test \
	51_static.test \
	52_unnamed_enum.test \
	54_goto.test \
	55_array_initializer.test \
	56_cross_structure.test \
	57_macro_bug.test \
	58_return_outside.test \
	59_break_before_loop.test \
	60_local_vars.test \
	61_initializers.test \
	62_float.test \
	63_typedef.test \
	64_double_prefix_op.test \
	66_printf_undefined.test \
	67_macro_crash.test \
	68_return.test \
	69_shebang_script.test \
	606_storm_inc_dec_member_call.test \
	607_storm_multi_ptr_loop_ternary.test \
	608_storm_void_ptr_list.test \
	609_storm_enum_union_fptr.test \
	610_storm_nested_loop_ternary_rec.test \
	611_storm_global_fptr_table.test \
	612_storm_struct_ptr_cast_chain.test \
	613_storm_ll_shift_loop.test \
	614_pseudo_dispatch_vm.test \
	615_pseudo_graph_dfs.test \
	616_storm_inc_dec_nested_ptr.test \
	617_linked_list_next.test \
	618_inc_member_call_arg.test \
	619_offsetof.test \
	620_forward_goto.test \
	621_csmith_struct_next_inc.test \
	622_csmith_next_chain.test

# include csmith/Makefile
# include jpoirier/Makefile

%.test: %.expect %.c
	@echo Test: $*...
	@if [ "x`echo $* | grep args`" != "x" ]; \
	then \
		../picoc $*.c - arg1 arg2 arg3 arg4 2>&1 >$*.output; \
	elif [ "x`echo $* | grep script`" != "x" ]; \
	then \
		../picoc -s $*.c 2>&1 >$*.output; \
	else \
		../picoc $*.c 2>&1 >$*.output; \
	fi
	@if [ "x`diff -qbu $*.expect $*.output`" != "x" ]; \
	then \
        	echo "error in test $*"; \
        	diff -u $*.expect $*.output; \
       		rm -f $*.output; \
		exit 1; \
	fi; \
       	rm -f $*.output

all: test

test: $(TESTS)
	@echo "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%"
	@echo "%%%%%%%%%%%% Tests Passed %%%%%%%%%%%%"
	@echo "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%"
	@echo

csmith: $(CSMITH_TESTS)
	@echo "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%"
	@echo "%%%%%%%%%%%% CSmith Tests Passed %%%%%%%%%%%%"
	@echo "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%"
	@echo

jpoirier: $(JPOIRIER_TESTS)
	@echo "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%"
	@echo "%%%%%%%%%%%% JPoirier Tests Passed %%%%%%%%%%%%"
	@echo "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%"
	@echo


# BabyPOSIX integration/regression tests (run from the interpreter build).
.PHONY: posix-tests
posix-tests:
	@set -e; for t in posix/*.c; do echo "POSIX test: $$t"; ../babyc "$$t"; done

# libWINAPI bridge regression tests (Linux host).
.PHONY: windows-tests
windows-tests:
	@set -e; cd ..; ./babyc tests/windows/winapi_console_file.c
