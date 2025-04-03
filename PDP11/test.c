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

	 	json_t *before = json_object();

		cpu_reset(&cpu_dev);

		saved_PC = 0100;
		json_object_set(before, "PC", json_integer(saved_PC));

		STACKFILE[0] = STACKFILE[1] = STACKFILE[2] = STACKFILE[3] = 010000;

		json_object_set(before, "stack-0", json_integer(STACKFILE[0]));
		json_object_set(before, "stack-1", json_integer(STACKFILE[1]));
		json_object_set(before, "stack-2", json_integer(STACKFILE[2]));
		json_object_set(before, "stack-3", json_integer(STACKFILE[3]));

		json_t *memory_i = json_array();

		json_t *put_mem_i_0 = json_object();
		PWriteW(i, saved_PC);
		json_object_set(put_mem_i_0, "0100", json_integer(i));
		json_array_append_new(memory_i, put_mem_i_0);

		json_t *put_mem_i_2 = json_object();
		uint16_t data1 = (rand() % 0160000) & (~1);
		PWriteW(data1, 0102);
		json_object_set(put_mem_i_2, "0102", json_integer(data1));
		json_array_append_new(memory_i, put_mem_i_2);

		json_t *put_mem_i_4 = json_object();
		uint16_t data2 = (rand() % 0160000) & (~1);
		PWriteW(data2, 0104);
		json_object_set(put_mem_i_4, "0104", json_integer(data2));
		json_array_append_new(memory_i, put_mem_i_4);

		json_object_set(before, "memory", memory_i);

		for(int k=0; k<6; k++) {
			char name[16];

			sprintf(name, "reg-%d.%d", k, 0);
			REGFILE[k][0] = (rand() % 0160000) & (~1);
			json_object_set(before, name, json_integer(REGFILE[k][0]));

			sprintf(name, "reg-%d.%d", k, 1);
			REGFILE[k][1] = (rand() % 0160000) & (~1);
			json_object_set(before, name, json_integer(REGFILE[k][1]));
		}

		// FIXME initialize PSW

		json_object_set(before, "PSW", json_integer(PSW));

		// do
		sim_instr();

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
		json_object_set(collection, "id", json_integer(i));
		json_object_set(collection, "before", before);
		json_object_set(collection, "after", after);

		json_array_append_new(out, collection);
	}

	FILE *fh = fopen("testset.json", "w");
	json_dumpf(out, fh, JSON_INDENT(2));
	fclose(fh);
}
