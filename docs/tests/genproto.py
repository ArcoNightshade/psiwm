import re,sys
src,dst=sys.argv[1],sys.argv[2]
s=open(src).read()
start=s.index('#include "if_iwm_proto.h"')
body=s[start:]
pat=re.compile(r'^(static (?:const )?(?:struct |enum |unsigned )?[A-Za-z_][A-Za-z0-9_]*(?: \*+)?)\n([A-Za-z_][A-Za-z0-9_]*)\(([^{;]*?)\)\n\{',re.M)
lines=['/*',' * This file and its contents are supplied under the terms of the',
' * Common Development and Distribution License ("CDDL"), version 1.0.',
' * You may only use this file in accordance with the terms of version',
' * 1.0 of the CDDL.',' *',
' * A full copy of the text of the CDDL should have accompanied this',
' * source.  A copy of the CDDL is also available via the Internet at',
' * http://www.illumos.org/license/CDDL.',' */','','/*',' * Copyright 2026 lex0de <lex0de@tuta.com>',' */','',
'/*',' * Prototypes for the static functions of if_iwm.c, in definition order.',
' * Regenerate with docs/tests/genproto.py after adding or changing one.',' */','',
'#ifndef _IF_IWM_PROTO_H','#define\t_IF_IWM_PROTO_H','','#ifdef __cplusplus','extern "C" {','#endif','']
def emit(text):
    out=[]; cur=text
    while len(cur.expandtabs(8))>80:
        cut=None
        for i in range(len(cur)-1):
            if cur[i]==',' and cur[i+1]==' ' and len(cur[:i+1].expandtabs(8))<=80: cut=i
        if cut is None:
            k=cur.find('(')
            if k>0 and k<len(cur)-1 and not out:
                out.append(cur[:k+1]); cur='    '+cur[k+1:]; continue
            break
        out.append(cur[:cut+1]); cur='    '+cur[cut+2:]
    out.append(cur); return out
n=0
for m in pat.finditer(body):
    rt,name,args=m.group(1),m.group(2),re.sub(r'\s+',' ',m.group(3)).strip()
    sep='' if rt.endswith('*') else ' '
    lines+=emit(rt+sep+name+'('+args+');'); n+=1
lines+=['','#ifdef __cplusplus','}','#endif','','#endif /* _IF_IWM_PROTO_H */','']
open(dst,'w').write('\n'.join(lines)); print(n,'prototypes')
