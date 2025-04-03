#include <jansson.h>
#include <stdio.h>

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

struct mem_t {
	uint32_t addr;
	uint16_t value;
};

json_t *generate_test(uint16_t instruction, int *const id, struct mem_t *mem, size_t n_mem)
{
	json_t *before = json_object();

	reset_all(0);
	cpu_reset(&cpu_dev);

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

	json_t *get_mem_i_0 = json_object();
	json_object_set(get_mem_i_0, "0100", json_integer(PReadW(0100)));
	json_array_append_new(memory_o, get_mem_i_0);

	json_t *get_mem_i_2 = json_object();
	json_object_set(get_mem_i_2, "0102", json_integer(PReadW(0102)));
	json_array_append_new(memory_o, get_mem_i_2);

	json_t *get_mem_i_4 = json_object();
	json_object_set(get_mem_i_4, "0104", json_integer(PReadW(0104)));
	json_array_append_new(memory_o, get_mem_i_4);

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
	reset_all(0);
	cpu_reset(&cpu_dev);
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

void emit_branch_instructions(json_t *const target, int *const id)
{
	for(int group=0; group<2; group++) {
		for(int bt=0; bt<8; bt++) {
			uint16_t instr = (group << 15) | (bt << 8);

			for(int psw_val=0; psw_val<16; psw_val++) {
				init_simh();

				saved_PC = 0100;

				randomize_registers_all_values();

				init_stack_registers();

				struct mem_t mem[1] = {
					{ 0100, instr }
				};

				PSW = psw_val;

				json_t *obj = generate_test(instr, id, mem, 1);
				if (obj)
					json_array_append_new(target, obj);
			}
		}
	}
}

void produce_validation_tests()
{
	json_t *out = json_array();

	uint32_t invalid[][2] = {
		{ 0000007, 0000077 },
		{ 0000210, 0000227 },
		{ 0007000, 0007777 },
		{ 0075040, 0076777 },
		{ 0106400, 0106477 },
		{ 0106700, 0107777 },
	};

	srand(123);  // for reproducability

	int id = 0;
#if 0
	for(int i=0; i<65536; i++) {
		int skip = 0;
		for(int test=0; test<6; test++) {
			if (i >= invalid[test][0] && i <= invalid[test][1]) {
				skip = 1;
				break;
			}
		}
		if (skip)
			continue;

		if (i == 0 ||  // HALT
		    i == 1 ||  // WAIT
		    i == 5)  // RESET
			continue;

		if (i >= 0170000 && i <= 0177777)  // FPU
			continue;

		init_simh();

		saved_PC = 0100;
		for(int k=0; k<6; k++) {
			REGFILE[k][0] = (rand() % 0160000) & (~1);
			REGFILE[k][1] = (rand() % 0160000) & (~1);
		}

		STACKFILE[0] = STACKFILE[1] = STACKFILE[2] = STACKFILE[3] = 010000;

		struct mem_t mem[3] = {
			{ 0100, i },
			{ 0102, (rand() % 0160000) & (~1) },
			{ 0104, (rand() % 0160000) & (~1) },
		};

		PSW = rand() & 15;  // only calculation status bits

		json_t *collection = generate_test(i, &id, mem, 3);
		json_array_append_new(out, collection);
	}
#endif

	emit_branch_instructions(out, &id);

	FILE *fh = fopen("testset.json", "w");
	json_dumpf(out, fh, JSON_INDENT(2));
	fclose(fh);
}
