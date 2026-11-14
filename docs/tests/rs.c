#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
typedef unsigned int uint_t; typedef int boolean_t;
#define B_TRUE 1
#define B_FALSE 0
#define IEEE80211_RATE_VAL 0x7f
#define IEEE80211_NODE_HT 0x40
#define IWM_RATE_MCS_HT_MSK (1<<8)
#define IWM_RATE_MCS_CCK_MSK (1<<9)
#define IWM_RATE_MCS_ANT_POS 14
typedef struct { struct {uint8_t ir_nrates; uint8_t ir_rates[15];} in_rates; struct {uint8_t rs_nrates; uint8_t rs_rates[127];} in_htrates; uint32_t in_flags; } ieee80211_node_t;
#include "extract.inc"
static void show(const char*t, ieee80211_node_t*n){struct iwm_rs_step l[IWM_RS_LADDER];uint_t c=iwm_rs_ladder(n,l);printf("%s: n=%u:",t,c);
 for(uint_t i=0;i<c;i++){uint32_t e;int r=iwm_tx_rate_encode(l[i].ht,l[i].rate,1,&e);printf(" %s%u(%s%x)",l[i].ht?"MCS":"L",l[i].rate,r?"ERR":"",e);}printf("\n");}
int main(){ieee80211_node_t n;
 memset(&n,0,sizeof n);n.in_rates.ir_nrates=12;memcpy(n.in_rates.ir_rates,(uint8_t[]){0x82,0x84,0x8b,0x96,12,18,24,36,48,72,96,108},12);
 show("11g",&n);
 n.in_flags=IEEE80211_NODE_HT;n.in_htrates.rs_nrates=8;for(int i=0;i<8;i++)n.in_htrates.rs_rates[i]=i;show("11n",&n);
 n.in_htrates.rs_nrates=3;memcpy(n.in_htrates.rs_rates,(uint8_t[]){5,0,2},3);show("11n-sparse-unsorted",&n);
 n.in_flags=0;n.in_rates.ir_nrates=4;show("11b",&n);
 n.in_rates.ir_nrates=0;show("empty",&n);
 n.in_flags=IEEE80211_NODE_HT;n.in_htrates.rs_nrates=8;for(int i=0;i<8;i++)n.in_htrates.rs_rates[i]=i;n.in_rates.ir_nrates=8;memcpy(n.in_rates.ir_rates,(uint8_t[]){12,18,24,36,48,72,96,108},8);show("11n-ofdm-only",&n);
 return 0;}
