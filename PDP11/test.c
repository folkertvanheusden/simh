#include <jansson.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

#include "pdp11_defs.h"
#include "sim_defs.h"

//extern t_stat sim_brk_init(void);
//extern t_stat sim_brk_set(t_addr loc, int32 sw, int32 ncnt, CONST char *act);
extern t_stat sim_instr();
//extern uint32 sim_brk_summ;
extern void PWriteW(int32 data, int32 addr);
extern int32 PReadW(int32 addr);
extern int32 REGFILE[6][2];
extern int32 STACKFILE[4];
extern int32 saved_PC;
extern int32 PSW;
extern t_stat cpu_reset(DEVICE *dptr);
extern DEVICE cpu_dev;
extern int32 STACKFILE[4];
extern int32 sim_interval;

struct mem_t {
	uint32_t addr;
	uint16_t value;
};

int is_prime(int n) {
    if (n <= 1)
	    return 0;
    for(int i = 2; i*i <= n; i++) {
	    if (n % i == 0)
		    return 0;
    }
    return 1;
}

int file_exist(const char *const filename)
{
	struct stat st;
	return stat(filename, &st) == 0;
}

uint16_t test_values[65536];
int n_test_values = 0;

void generate_test_values()
{
	for(int i=1; i<65536; i+=28) {
		if (is_prime(i))
			test_values[n_test_values++] = i;
	}

	for(int i=1; i<16; i++)
		test_values[n_test_values++] = 1 << i;

	test_values[n_test_values++] = 0;
	test_values[n_test_values++] = 255;
	test_values[n_test_values++] = 32767;
	test_values[n_test_values++] = 65535;
}

json_t *generate_test(uint16_t instruction, int *const id, struct mem_t *mem, size_t n_mem)
{
	json_t *before = json_object();

	json_object_set(before, "PC", json_integer(saved_PC));

	json_object_set(before, "stack-0", json_integer(STACKFILE[0]));
	json_object_set(before, "stack-1", json_integer(STACKFILE[1]));
	json_object_set(before, "stack-2", json_integer(STACKFILE[2]));
	json_object_set(before, "stack-3", json_integer(STACKFILE[3]));

	json_t *memory_i = json_array();

	for(size_t i=0; i<n_mem; i++) {
		char buffer[16];
		json_t *put_mem_i_0 = json_object();
		PWriteW(mem[i].value, mem[i].addr);

		sprintf(buffer, "%06o", mem[i].addr);
		json_object_set(put_mem_i_0, buffer, json_integer(mem[i].value));
		json_array_append_new(memory_i, put_mem_i_0);
	}

	json_object_set(before, "memory", memory_i);

	for(int k=0; k<6; k++) {
		char name[16];

		sprintf(name, "reg-%d.%d", k, 0);
		json_object_set(before, name, json_integer(REGFILE[k][0]));

		sprintf(name, "reg-%d.%d", k, 1);
		json_object_set(before, name, json_integer(REGFILE[k][1]));
	}

	json_object_set(before, "PSW", json_integer(PSW));

	// do
	int failed = sim_instr() != 0;

	json_t *after = json_object();
	json_object_set(after, "PC", json_integer(saved_PC));

	for(int k=0; k<6; k++) {
		char name[16];

		sprintf(name, "reg-%d.%d", k, 0);
		json_object_set(after, name, json_integer(REGFILE[k][0]));

		sprintf(name, "reg-%d.%d", k, 1);
		json_object_set(after, name, json_integer(REGFILE[k][1]));
	}

	json_object_set(after, "PSW", json_integer(PSW));

	json_object_set(after, "stack-0", json_integer(STACKFILE[0]));
	json_object_set(after, "stack-1", json_integer(STACKFILE[1]));
	json_object_set(after, "stack-2", json_integer(STACKFILE[2]));
	json_object_set(after, "stack-3", json_integer(STACKFILE[3]));

	json_t *memory_o = json_array();

	for(size_t i=0; i<n_mem; i++) {
		char buffer[16];
		sprintf(buffer, "%06o", mem[i].addr);

		json_t *get_mem = json_object();
		json_object_set(get_mem, buffer, json_integer(PReadW(mem[i].addr)));
		json_array_append_new(memory_o, get_mem);
	}

	json_object_set(after, "memory", memory_o);

	json_t *collection = json_object();
	json_object_set(collection, "id", json_integer(*id));
	(*id)++;
	json_object_set(collection, "before", before);
	json_object_set(collection, "after", after);

	if (failed) {
		json_decref(collection);
		return NULL;
	}

	return collection;
}

void init_simh()
{
	// reset_all(0);  is this required?
	cpu_reset(&cpu_dev);
	sim_interval = 32767;
}

void randomize_registers_all_values()
{
	for(int k=0; k<6; k++) {
		REGFILE[k][0] = rand() & 0xffff;
		REGFILE[k][1] = rand() & 0xffff;
	}
}

void init_stack_registers()
{
	STACKFILE[0] = STACKFILE[1] = STACKFILE[2] = STACKFILE[3] = 010000;
}

void produce_set_register(const uint16_t instr, const uint16_t psw, int *const id, json_t *const out)
{
	for(int v1=0; v1<n_test_values; v1++) {
		for(int v2=0; v2<n_test_values; v2++) {
			init_simh();

			saved_PC = 0100;

			randomize_registers_all_values();
			REGFILE[0][0] = REGFILE[0][1] = test_values[v1];
			REGFILE[1][0] = REGFILE[1][1] = test_values[v2];

			init_stack_registers();

			struct mem_t mem[1] = {
				{ 0100, instr }
			};

			PSW = psw;

			json_t *obj = generate_test(instr, id, mem, 1);
			if (obj)
				json_array_append_new(out, obj);
		}
	}
}

void produce_set_register_indirect(const uint16_t instr, const uint16_t psw, int *const id, json_t *const out)
{
	if (((instr >> 3) & 7) != 1 || (instr & 7) != 1) {
		printf("Not an R1 target!");
		return;
	}

	for(int v1=0; v1<n_test_values; v1++) {
		for(int v2=0; v2<n_test_values; v2++) {
			init_simh();

			saved_PC = 0100;

			randomize_registers_all_values();
			REGFILE[0][0] = REGFILE[0][1] = test_values[v1];
			REGFILE[1][0] = REGFILE[1][1] = 01002;

			init_stack_registers();

			struct mem_t mem[2] = {
				{ 0100, instr },
				{ 01002, test_values[v2] }
			};

			PSW = psw;

			json_t *obj = generate_test(instr, id, mem, 2);
			if (obj)
				json_array_append_new(out, obj);
		}
	}
}

void dump_json(const char *const filename, json_t *const j)
{
	FILE *fh = fopen(filename, "w");
	json_dumpf(j, fh, JSON_INDENT(1));
	fclose(fh);

	json_decref(j);
}

void emit_branch_instructions()
{
	printf("Branch instructions\n");
	const char *const filename = "pdp1170-valtest-COND-BRANCHES.json";
	if (file_exist(filename))
		return;
	int id = 0;
	json_t *out = json_array();
	for(int group=0; group<2; group++) {  // word/byte
		for(int bt=0; bt<8; bt++) {
			if (group == 0 && bt == 0)  // SWAB
				continue;
			for(int direction=0; direction<4; direction++) {
				uint16_t instr = (group << 15) | (bt << 8);

				if (direction & 2)
					instr |= 62 + (direction & 1);
				else
					instr |= 218 + (direction & 1);

				for(int psw_val=0; psw_val<16; psw_val++) {
					init_simh();

					saved_PC = 0100;

					randomize_registers_all_values();

					init_stack_registers();

					struct mem_t mem[1] = {
						{ 0100, instr }
					};

					PSW = psw_val;

					json_t *obj = generate_test(instr, &id, mem, 1);
					if (obj)
						json_array_append_new(out, obj);
				}
			}
		}
	}
	dump_json(filename, out);
}

void emit_condition_sets()
{
	printf("Condition set instructions\n");
	const char *const filename = "pdp1170-valtest-CONDITIONS.json";
	if (file_exist(filename))
		return;
	int id = 0;
	json_t *out = json_array();
	for(int condition=0; condition<16; condition++) {
		uint16_t instr = 0240 + condition;

		for(int psw_val=0; psw_val<16; psw_val++) {
			init_simh();

			saved_PC = 0100;

			randomize_registers_all_values();

			init_stack_registers();

			struct mem_t mem[1] = {
				{ 0100, instr }
			};

			PSW = psw_val;

			json_t *obj = generate_test(instr, &id, mem, 1);
			if (obj)
				json_array_append_new(out, obj);
		}
	}
	dump_json(filename, out);
}

void emit_add_sub_c()
{
	printf("ADD/SUB/ADC/SBC instructions\n");
	const char *const filename = "pdp1170-valtest-ADD_SUB_ADC_SBC.json";
	if (file_exist(filename))
		return;
	int id = 0;
	json_t *out = json_array();
	for(int group=0; group<4; group++) {
		uint16_t instr = 0;
		int      word  = group & 1;

		if (group == 0 || group == 1)
			instr = (6 << 12 /* instr */) | (word << 15 /* ADD/SUB */) | (1 << 6 /* src=R1 */);
		else if (group == 2)
			instr = (055 << 6 /* instr */) | (word << 15 /* ADCb/ADCw */) | (1 << 6 /* src=R1 */);
		else if (group == 3)
			instr = (056 << 6 /* instr */) | (word << 15 /* SBCb/SBCw */) | (1 << 6 /* src=R1 */);

		for(int psw_val=0; psw_val<2; psw_val++)
			produce_set_register(instr, psw_val, &id, out);
	}
	dump_json(filename, out);
}

void emit_single_operand_instructions()
{
	printf("single operand instructions\n");
	int groups[] = { 00001, 00003, 00050, 01050, 00051, 01051, 00052, 01052, 00053, 01053, 00054, 01054, 00057, 01057, 00060, 01060, 00061, 01061, 00062, 01062, 00063, 01063, 00067, -1 };

	for(int group=0; group<23; group++) {
		char filename[128] = { 0 };
		sprintf(filename, "pdp1170-valtest-SINGLE_OPERAND-%o.json", groups[group]);
		if (file_exist(filename))
			continue;
		printf("%s       \r", filename);
		fflush(NULL);

		int id = 0;
		json_t *out = json_array();

		uint16_t instr = 0;

		if (groups[group] == 00001)  // JMP can't jump to a register
			instr = (groups[group] << 6) | 010;  // (R0)
		else
			instr = (groups[group] << 6) | 001;  // R0

		for(int v1=0; v1<n_test_values; v1++) {
			for(int psw_val=0; psw_val<2; psw_val++) {
				init_simh();

				saved_PC = 0100;

				randomize_registers_all_values();
				REGFILE[0][0] = REGFILE[0][1] = test_values[v1];

				init_stack_registers();

				struct mem_t mem[1] = {
					{ 0100, instr }
				};

				PSW = psw_val;

				json_t *obj = generate_test(instr, &id, mem, 1);
				if (obj)
					json_array_append_new(out, obj);
			}
		}
		dump_json(filename, out);
	}
}

void emit_cmp()
{
	printf("CMP instructions\n");
	const char *const filename = "pdp1170-valtest-CMP.json";
	if (file_exist(filename))
		return;
	int id = 0;
	json_t *out = json_array();
	for(int group=0; group<2; group++) {
		uint16_t instr = 0;
		int      word  = group & 1;

		if (group == 0 || group == 1)
			instr = (2 << 12 /* instr */) | (word << 15 /* CMPb/CMPw */) | (1 << 6 /* src=R1 */);

		for(int psw_val=0; psw_val<2; psw_val++)
			produce_set_register(instr, psw_val, &id, out);
	}
	dump_json(filename, out);
}

void emit_add_double_oper_instr()
{
	printf("addition double operand instructions\n");
	const char *const filename = "pdp1170-valtest-DOUB_OPER-INSTR.json";
	if (file_exist(filename))
		return;
	int id = 0;
	json_t *out = json_array();
	for(int group=0; group<8; group++) {
		uint16_t instr = (7 << 12) | (group << 9 /* instr */) | (2 << 6 /* src=R2 */);

		if (group == 5 || group == 6)
			continue;

		produce_set_register(instr, 0, &id, out);
	}
	dump_json(filename, out);
}

void emit_bit_instructions()
{
	printf("bit instructions\n");
	for(int group=3; group<6; group++) {
		char filename[64];
		sprintf(filename, "pdp1170-valtest-BIT-INSTRUCTIONS-%d.json", group);
		if (file_exist(filename))
			return;
		int id = 0;
		json_t *out = json_array();
		for(int word=0; word<2; word++) {
			uint16_t instr = (word << 15) | (group << 12) | 011 /* target is '(R1)' */;

			for(int psw_val=0; psw_val<2; psw_val++) {
				produce_set_register(instr, psw_val, &id, out);

				if (group == 4 || group == 5)
					produce_set_register_indirect(instr, psw_val, &id, out);
			}
		}
		dump_json(filename, out);
	}
}

void emit_misc_operations()
{
	printf("misc instructions\n");

	const char *const filename = "pdp1170-valtest-MISC.json";
	if (file_exist(filename))
		return;

	int id = 0;
	json_t *out = json_array();

	int groups[] = { 2 /* RTI */, 6 /* RTT */ };

	for(int group=0; group<2; group++) {
		for(int psw_val=0; psw_val<65536; psw_val++) {
			if ((psw_val & 0174377) != psw_val)
				continue;

			uint16_t instr = groups[group];

			init_simh();

			saved_PC = 0100;
			randomize_registers_all_values();
			init_stack_registers();

			struct mem_t mem[1] = {
				{ 0100, instr }
			};

			PSW = psw_val;

			json_t *obj = generate_test(instr, &id, mem, 1);
			if (obj)
				json_array_append_new(out, obj);
		}
	}

	for(int group=0; group<3; group++) {
		uint16_t instr = 0;
		if (group == 0)
			instr = 0100 | 010;  // JMP (R0)
		else if (group == 1)
			instr = 04000 | 0110;  // JSR R1,(R0)
		else if (group == 2)
			instr = 0200 | 01;  // RTS (R1)

		produce_set_register(instr, 0, &id, out);
	}

	dump_json(filename, out);
}

void produce_validation_tests()
{
	srand(123);  // for reproducability

	generate_test_values();

	emit_branch_instructions();  // conditional_branch_instructions*
	emit_condition_sets();  // condition_code_operations*
	emit_add_double_oper_instr();  // additional_double_operand_instructions*
	emit_add_sub_c();  // double_operand_instructions*, single_operand_instructions*
	emit_single_operand_instructions();  // single_operand_instructions
	emit_bit_instructions();  // double_operand_instructions
	emit_cmp();  // double_operand_instructions
	emit_misc_operations();

	// TODO:
	// - double_operand_instructions: MOV
	// - single_operand_instructions: MTPS, MFPI, MFPD, MTPI, MTPD, MFPS
	// ...
}
