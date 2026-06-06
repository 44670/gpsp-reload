function track(field, max, group) {
	fields[++field_count] = field;
	field_max[field] = max;
	field_group[field] = group;
	if (!(group in group_seen)) {
		groups[++group_count] = group;
		group_seen[group] = 1;
	}
}

function value_of(field, i, kv) {
	for (i = 1; i <= NF; i++) {
		split($i, kv, "=");
		if (kv[1] == field)
			return kv[2];
	}
	return "";
}

function fail(message) {
	print "runtime size gate failed: " message > "/dev/stderr";
	failed = 1;
	exit;
}

function require_eq(field, expected, value) {
	value = value_of(field);
	if (value == "")
		fail("missing field=" field);
	if ((value + 0) != expected)
		fail("field=" field " value=" value " expected=" expected);
}

BEGIN {
	track("thumb_simple_data_code_bytes", 152, "thumb_direct");
	track("thumb_hi_cmp_code_bytes", 140, "thumb_direct");
	track("thumb_flag_alu_code_bytes", 136, "thumb_direct");
	track("thumb_memory_load_code_bytes", 116, "thumb_direct");
	track("thumb_memory_store_code_bytes", 148, "thumb_direct");
	track("thumb_reg_shift_code_bytes", 736, "thumb_direct");
	track("thumb_block_store_code_bytes", 108, "thumb_direct");
	track("thumb_block_load_code_bytes", 120, "thumb_direct");
	track("thumb_block_push_code_bytes", 132, "thumb_direct");
	track("thumb_block_pop_pc_code_bytes", 112, "thumb_direct");

	track("multiply_code_bytes", 96, "arm_multiply");
	track("multiply_flag_muls_code_bytes", 104, "arm_multiply");
	track("multiply_flag_mlas_code_bytes", 116, "arm_multiply");
	track("multiply_long_code_bytes", 104, "arm_multiply");
	track("multiply_long_flag_umulls_code_bytes", 120, "arm_multiply");
	track("multiply_long_flag_smulls_code_bytes", 120, "arm_multiply");
	track("multiply_long_acc_code_bytes", 152, "arm_multiply");
	track("multiply_long_acc_flag_umlals_code_bytes", 144, "arm_multiply");
	track("multiply_long_acc_flag_smlals_code_bytes", 144, "arm_multiply");

	track("carry_data_code_bytes", 148, "arm_flags_shift");
	track("cmp_borrow_code_bytes", 136, "arm_flags_shift");
	track("cmp_equal_code_bytes", 136, "arm_flags_shift");
	track("tst_simple_code_bytes", 100, "arm_flags_shift");
	track("tst_shift_code_bytes", 120, "arm_flags_shift");
	track("cmn_overflow_code_bytes", 136, "arm_flags_shift");
	track("teq_simple_code_bytes", 100, "arm_flags_shift");
	track("flag_subs_code_bytes", 140, "arm_flags_shift");
	track("flag_subs_nz_code_bytes", 104, "arm_flags_shift");
	track("flag_adds_code_bytes", 140, "arm_flags_shift");
	track("flag_rsbs_code_bytes", 136, "arm_flags_shift");
	track("carry_flag_adcs_code_bytes", 164, "arm_flags_shift");
	track("carry_flag_sbcs_code_bytes", 164, "arm_flags_shift");
	track("carry_flag_rscs_code_bytes", 160, "arm_flags_shift");
	track("logical_flag_code_bytes", 448, "arm_flags_shift");
	track("data_ext_code_bytes", 244, "arm_flags_shift");
	track("reg_shift_data_code_bytes", 212, "arm_flags_shift");
	track("reg_shift_flag_lsl_code_bytes", 172, "arm_flags_shift");
	track("reg_shift_flag_lsr_code_bytes", 172, "arm_flags_shift");
	track("reg_shift_flag_asr_code_bytes", 172, "arm_flags_shift");
	track("reg_shift_flag_ror0_code_bytes", 160, "arm_flags_shift");
	track("reg_shift_test_code_bytes", 168, "arm_flags_shift");
	track("pc_source_code_bytes", 388, "arm_control_flow");
	track("conditional_code_bytes", 1152, "arm_control_flow");
	track("pc_write_mov_code_bytes", 48, "arm_control_flow");
	track("pc_write_movs_code_bytes", 96, "arm_control_flow");
	track("pc_write_add_code_bytes", 52, "arm_control_flow");
	track("branch_code_bytes", 52, "arm_control_flow");
	track("internal_branch_code_bytes", 96, "arm_control_flow");
	track("external_branch_code_bytes", 60, "arm_control_flow");
	track("bl_code_bytes", 64, "arm_control_flow");
	track("bx_code_bytes", 76, "arm_control_flow");

	track("psr_code_bytes", 96, "arm_psr");
	track("msr_cpsr_flags_code_bytes", 80, "arm_psr");
	track("msr_cpsr_control_code_bytes", 68, "arm_psr");
	track("msr_spsr_code_bytes", 68, "arm_psr");

	track("load_code_bytes", 112, "arm_word_memory");
	track("load_pc_code_bytes", 68, "arm_word_memory");
	track("load_byte_pc_code_bytes", 68, "arm_word_memory");
	track("pc_base_load_code_bytes", 112, "arm_word_memory");
	track("pc_base_store_code_bytes", 84, "arm_word_memory");
	track("store_word_code_bytes", 84, "arm_word_memory");
	track("store_alert_chain_code_bytes", 64, "arm_word_memory");
	track("store_byte_code_bytes", 84, "arm_word_memory");
	track("store_pc_code_bytes", 88, "arm_word_memory");
	track("reg_offset_load_code_bytes", 120, "arm_word_memory");
	track("reg_offset_pc_load_code_bytes", 124, "arm_word_memory");
	track("reg_offset_pc_store_code_bytes", 92, "arm_word_memory");
	track("reg_offset_pc_byte_store_code_bytes", 92, "arm_word_memory");
	track("reg_offset_store_code_bytes", 88, "arm_word_memory");
	track("shifted_reg_offset_pc_lsl_word_code_bytes", 88, "arm_word_memory");
	track("shifted_reg_offset_pc_lsl_word_store_code_bytes", 92, "arm_word_memory");
	track("shifted_reg_offset_pc_lsr_word_code_bytes", 88, "arm_word_memory");
	track("shifted_reg_offset_pc_lsr_word_store_code_bytes", 92, "arm_word_memory");
	track("shifted_reg_offset_pc_asr_word_code_bytes", 88, "arm_word_memory");
	track("shifted_reg_offset_pc_asr_word_store_code_bytes", 92, "arm_word_memory");
	track("shifted_reg_offset_pc_ror_word_code_bytes", 84, "arm_word_memory");
	track("shifted_reg_offset_pc_ror_word_store_code_bytes", 92, "arm_word_memory");

	track("half_load_pc_code_bytes", 68, "arm_half_signed_memory");
	track("half_reg_load_pc_code_bytes", 72, "arm_half_signed_memory");
	track("signed_byte_reg_load_pc_code_bytes", 72, "arm_half_signed_memory");
	track("signed_half_reg_load_pc_code_bytes", 76, "arm_half_signed_memory");
	track("signed_byte_load_pc_code_bytes", 68, "arm_half_signed_memory");
	track("signed_half_load_pc_code_bytes", 72, "arm_half_signed_memory");
	track("pc_base_half_load_code_bytes", 144, "arm_half_signed_memory");
	track("pc_base_half_store_code_bytes", 84, "arm_half_signed_memory");
	track("pc_base_half_neg_store_code_bytes", 84, "arm_half_signed_memory");
	track("half_load_code_bytes", 148, "arm_half_signed_memory");
	track("half_store_code_bytes", 84, "arm_half_signed_memory");
	track("half_reg_load_code_bytes", 160, "arm_half_signed_memory");
	track("half_reg_pc_load_code_bytes", 172, "arm_half_signed_memory");
	track("half_reg_pc_store_code_bytes", 88, "arm_half_signed_memory");
	track("half_reg_store_code_bytes", 88, "arm_half_signed_memory");
	track("half_writeback_store_code_bytes", 88, "arm_half_signed_memory");
	track("half_writeback_load_code_bytes", 92, "arm_half_signed_memory");
	track("reg_offset_rrx_load_code_bytes", 108, "arm_half_signed_memory");
	track("reg_offset_rrx_store_code_bytes", 112, "arm_half_signed_memory");
	track("shifted_reg_offset_code_bytes", 128, "arm_half_signed_memory");
	track("shifted_reg_offset_store_code_bytes", 92, "arm_half_signed_memory");
	track("shifted_reg_offset_pc_store_code_bytes", 92, "arm_half_signed_memory");
	track("shifted_reg_offset_pc_lsr_code_bytes", 88, "arm_half_signed_memory");
	track("shifted_reg_offset_pc_lsr_store_code_bytes", 92, "arm_half_signed_memory");
	track("shifted_reg_offset_pc_asr_code_bytes", 88, "arm_half_signed_memory");
	track("shifted_reg_offset_pc_asr_store_code_bytes", 92, "arm_half_signed_memory");
	track("shifted_reg_offset_pc_ror_code_bytes", 84, "arm_half_signed_memory");
	track("shifted_reg_offset_pc_ror_store_code_bytes", 92, "arm_half_signed_memory");
	track("shifted_reg_offset_lsr_code_bytes", 84, "arm_half_signed_memory");
	track("shifted_reg_offset_asr_code_bytes", 88, "arm_half_signed_memory");
	track("shifted_reg_offset_ror_code_bytes", 96, "arm_half_signed_memory");
	track("shifted_reg_offset_lsr_store_code_bytes", 92, "arm_half_signed_memory");
	track("shifted_reg_offset_asr_store_code_bytes", 92, "arm_half_signed_memory");
	track("shifted_reg_offset_ror_store_code_bytes", 100, "arm_half_signed_memory");
	track("writeback_store_code_bytes", 88, "arm_half_signed_memory");
	track("writeback_load_code_bytes", 80, "arm_half_signed_memory");
	track("reg_offset_writeback_store_code_bytes", 92, "arm_half_signed_memory");
	track("reg_offset_writeback_load_code_bytes", 88, "arm_half_signed_memory");

	track("block_mem_stm_code_bytes", 140, "arm_block_memory");
	track("block_mem_ldm_code_bytes", 124, "arm_block_memory");
	track("block_mem_push_code_bytes", 148, "arm_block_memory");
	track("block_mem_ldm_pc_code_bytes", 112, "arm_block_memory");
	track("block_mem_ldm_base_list_code_bytes", 116, "arm_block_memory");
	track("block_mem_ldm_pc_s_code_bytes", 64, "arm_block_memory");

	track("swp_word_code_bytes", 68, "arm_swap");
	track("swp_byte_code_bytes", 68, "arm_swap");

	track("swi_code_bytes", 56, "arm_swi_hle");
	track("swi_patch_code_bytes", 64, "arm_swi_hle");
	track("swi_target_code_bytes", 64, "arm_swi_hle");
	track("hle_div_code_bytes", 64, "arm_swi_hle");
	track("hle_divarm_code_bytes", 64, "arm_swi_hle");
}

/result=PASS command=runtime/ {
	seen = 1;
	require_eq("final_thumb_helpers", 0);
	require_eq("first_exec_fallbacks", 0);

	for (i = 1; i <= field_count; i++) {
		field = fields[i];
		value = value_of(field);
		if (value == "")
			fail("missing field=" field);
		if ((value + 0) <= 0)
			fail("nonpositive field=" field " value=" value);
		if ((value + 0) > field_max[field])
			fail("field=" field " value=" value " max=" field_max[field]);

		group = field_group[field];
		group_total[group] += value + 0;
		group_max[group] += field_max[field];
		arm_tracked_total += (group ~ /^arm_/) ? value + 0 : 0;
		arm_tracked_max += (group ~ /^arm_/) ? field_max[field] : 0;

		printf("runtime_code_size field=%s group=%s code_bytes=%s max=%u\n",
		       field, group, value, field_max[field]);
	}

	for (i = 1; i <= group_count; i++) {
		group = groups[i];
		printf("runtime_code_size group=%s total=%u max=%u\n",
		       group, group_total[group], group_max[group]);
	}
	printf("runtime_code_size arm_tracked_total=%u arm_tracked_max=%u thumb_helpers=0 first_exec_fallbacks=0\n",
	       arm_tracked_total, arm_tracked_max);
}

END {
	if (failed)
		exit 1;
	if (!seen) {
		print "runtime size gate failed: missing runtime result" > "/dev/stderr";
		exit 1;
	}
}
