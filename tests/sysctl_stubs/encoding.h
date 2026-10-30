#include <stdint.h>
#define MIP_MEIP (1U << 11)
#define MSTATUS_MIE (1U << 3)
#define set_csr(reg, value) ((void)(value))
#define clear_csr(reg, value) ((void)(value))
#define read_cycle() ((uint64_t)0U)
