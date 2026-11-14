#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "if_iwmfw.h"
/* instrumented copy: report TLV types and sections */
static uint32_t u32(const uint8_t *p){return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;}
int main(int c,char**v){
 FILE*f=fopen(v[1],"rb");fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
 uint8_t*d=malloc(n);fread(d,1,n,f);
 struct iwm_fw_info fw;memset(&fw,0,sizeof fw);fw.data=d;fw.size=n;
 int e=iwm_fw_parse(&fw);
 printf("%s: parse=%d ver=%u/%08x/%u flags=%08x phy=%08x paging=%u scan=%u\n",v[1],e,fw.version[0],fw.version[1],fw.version[2],fw.flags,fw.phy_config,fw.paging_size,fw.scan_channels);
 const uint8_t*p=d+88;size_t left=n-88;
 while(left>=8){uint32_t t=u32(p),l=u32(p+4);p+=8;left-=8;if(l>left)break;
  if(t==19||t==20||t==21)printf("  sec type=%u off=%08x len=%u\n",t,u32(p),l-4);
  else if(t==36)printf("  ver %u %08x %u\n",u32(p),u32(p+4),u32(p+8));
  else if(t==29||t==30)printf("  %s[%u]=%08x\n",t==29?"api":"capa",u32(p),u32(p+4));
  else printf("  tlv %u len %u%s\n",t,l,l==4?"":"");
  size_t a=(l+3)&~3u;if(a>left)a=left;p+=a;left-=a;}
 return 0;}
