#include <jansson.h>
#include <stdio.h>
#include <time.h>

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

int is_prime(int n) {
    if (n <= 1)
	    return 0;
    for(int i = 2; i*i <= n; i++) {
	    if (n % i == 0)
		    return 0;
    }
    return 1;
}

uint16_t test_values[65536];
int n_test_values = 0;

void generate_test_values()
{
	for(int i=1; i<65536; i+=10) {
		if (is_prime(i))
			test_values[n_test_values++] = i;
	}

	for(int i=2; i<16; i++)
		test_values[n_test_values++] = 1 << i;

	test_values[n_test_values++] = 0;
	test_values[n_test_values++] = 255;
	test_values[n_test_values++] = 32767;
	test_values[n_test_values++] = 65535;

	printf("%d\n", n_test_values);
}

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
	// reset_all(0);  is this required?
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
	printf("Branch instructions\n");
	for(int group=0; group<2; group++) {
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

					json_t *obj = generate_test(instr, id, mem, 1);
					if (obj)
						json_array_append_new(target, obj);
				}
			}
		}
	}
}

void emit_condition_sets(json_t *const target, int *const id)
{
	printf("Condition set instructions\n");
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

			json_t *obj = generate_test(instr, id, mem, 1);
			if (obj)
				json_array_append_new(target, obj);
		}
	}
}

void emit_add_sub(json_t *const target, int *const id)
{
	int count = 0;
	int total = n_test_values * n_test_values * 2;
	time_t start = time(NULL);
	printf("ADD/SUB instructions\n");
	for(int group=0; group<2; group++) {
		uint16_t instr = (6 << 12 /* instr */) | (group << 15 /* ADD/SUB */) | (1 << 6 /* src=R1 */);

		for(int v1=0; v1<n_test_values; v1++) {
			for(int v2=0; v2<n_test_values; v2++) {
				count++;

				init_simh();

				saved_PC = 0100;

				randomize_registers_all_values();
				REGFILE[0][0] = REGFILE[0][1] = v1;
				REGFILE[1][0] = REGFILE[1][1] = v2;

				init_stack_registers();

				struct mem_t mem[1] = {
					{ 0100, instr }
				};

				PSW = 0;

				json_t *obj = generate_test(instr, id, mem, 1);
				if (obj)
					json_array_append_new(target, obj);
			}

			printf("%.2f%% %f      \r", count * 100 / (double)total, total / (double)count * (time(NULL) - start));
			fflush(NULL);
		}
	}
}

void produce_validation_tests()
{
	json_t *out = json_array();

	generate_test_values();

	srand(123);  // for reproducability

	int id = 0;

	// TODO FIXME opslaan per type in json, id als md5?
//	emit_branch_instructions(out, &id);

//	emit_condition_sets(out, &id);

	emit_add_sub(out, &id);

	FILE *fh = fopen("testset.json", "w");
	json_dumpf(out, fh, JSON_INDENT(2));
	fclose(fh);
}
