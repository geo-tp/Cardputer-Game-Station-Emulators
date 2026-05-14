
#ifndef NEC_H_
#define NEC_H_

#include <stdint.h>

enum {
    NEC_IP=1, NEC_AW, NEC_CW, NEC_DW, NEC_BW, NEC_SP, NEC_BP, NEC_IX, NEC_IY,
    NEC_FLAGS, NEC_ES, NEC_CS, NEC_SS, NEC_DS,
    NEC_VECTOR, NEC_PENDING, NEC_NMI_STATE, NEC_IRQ_STATE };

typedef struct nec_context {
    uint16_t regs[8];
    uint16_t sregs[4];
    uint16_t ip;
    uint16_t flags;
    uint32_t int_vector;
    uint32_t pending_irq;
    uint32_t nmi_state;
    uint32_t irq_state;
    int32_t no_interrupt;
    int32_t seg_prefix;
    uint32_t prefix_base;
    int32_t icount;
} nec_context;

/* Public variables */
extern int nec_ICount;

void nec_set_reg(int,unsigned);
int nec_execute(int cycles);    
unsigned nec_get_reg(int regnum);
void nec_get_context(nec_context* dst);
void nec_set_context(const nec_context* src);
void nec_reset (void *param);
void nec_int(unsigned long wektor);
#if defined(WS_CPU_PROFILE) || defined(WS_CPU_BRANCH_PROFILE)
void nec_profile_log_and_reset(void);
#endif

#endif
