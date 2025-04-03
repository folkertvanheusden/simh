#include <jansson.h>
#include <stdio.h>

#include "pdp11_defs.h"
#include "sim_defs.h"

//extern t_stat sim_brk_init(void);
//extern t_stat sim_brk_set(t_addr loc, int32 sw, int32 ncnt, CONST char *act);
extern t_stat sim_instr();
//extern uint32 sim_brk_summ;
extern void PWriteW(int32 data, int32 addr);
extern int32 REGFILE[6][2];
extern int32 STACKFILE[4];
extern int32 saved_PC;
extern int32 PSW;
extern t_stat cpu_reset(DEVICE *dptr);
extern DEVICE cpu_dev;

void produce_validation_tests()
{
	json_t *out = json_array();

	for(int i=0; i<65536; i++) {
		json_t *before = json_object();

		cpu_reset(&cpu_dev);

		saved_PC = 0100;
		json_object_set(before, "PC", json_integer(saved_PC));

		json_t *memory = json_array();
		json_t *mem_i = json_object();
		json_object_set(mem_i, "0100", json_integer(i));
		json_array_append_new(memory, mem_i);
		json_object_set(before, "memory", memory);
		PWriteW(i, saved_PC);
		uint16_t data1 = rand() & 0xffff;
		uint16_t data2 = rand() & 0xffff;
		json_object_set(mem_i, "0102", json_integer(data1));
		PWriteW(data1, saved_PC + 2);
		json_object_set(mem_i, "0104", json_integer(data2));
		PWriteW(data2, saved_PC + 4);

		for(int k=0; k<6; k++) {
			char name[16];

			sprintf(name, "reg-%d.%d", k, 0);
			REGFILE[k][0] = rand() & 0xffff;
			json_object_set(before, name, json_integer(REGFILE[k][0]));

			sprintf(name, "reg-%d.%d", k, 1);
			REGFILE[k][1] = rand() & 0xffff;
			json_object_set(before, name, json_integer(REGFILE[k][1]));
		}

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

		json_t *collection = json_object();
		json_object_set(collection, "before", before);
		json_object_set(collection, "after", after);

		json_array_append_new(out, collection);
	}

	FILE *fh = fopen("testset.json", "w");
	json_dumpf(out, fh, JSON_INDENT(2));
	fclose(fh);
}
