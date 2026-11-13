# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import sys,json,hashlib
source,output=map(Path,sys.argv[1:]);reference=json.loads(source.read_text())
rows=[r for r in reference['trace'] if r['input'][0]<4 and r['input'][1] in [2,3,4,5,6,7,63,127]]
assert len(rows)==32
kinds=sorted({key>>32 for r in rows for group in ['players','stage'] for key in r[group]});assert kinds==list(range(7))
report={'rows':32,'source':str(source),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'kinds':kinds,'scope':'32 selected original-method display-list snapshots supplied as GPU draw-order inputs. Two quality levels, three camera scales and two root shifts yield 384 compositions. Expected pixels use explicit per-item drawing in the supplied original order; this isolates native dispatch/material/order, not original Flash raster or display-container internals. A separate pixel-center aperture invariant checks black letterbox columns.','trace':rows}
output.write_text(json.dumps(report,indent=2)+'\n')
output.with_suffix('.IN').write_text(str(len(rows))+'\n'+'\n'.join(' '.join(map(str,[r['input'][0],r['input'][1],*[v for g in ['players','stage'] for v in [len(r[g]),*r[g]]]])) for r in rows)+'\n')
print('GPU original-order snapshots',len(rows),'kinds',kinds)
