#!/usr/bin/env python3
"""Record the canonical source canvases used by an existing fighter export."""
import argparse
from pathlib import Path
from pack_fighter import read_timeline
p=argparse.ArgumentParser(description=__doc__);p.add_argument('export',type=Path);p.add_argument('output',type=Path);a=p.parse_args();t=read_timeline(a.export)
b={str(f):pose for f,pose in t.poses.items() if pose}
b.update({f'{f}-{phase}':pose for (f,phase),pose in t.children.items() if pose})
for objects in [t.projectiles,t.attackers]:
 for obj in objects.values():
  for pose in obj.poses.values():
   if pose:b[pose[0]]=pose[1:]
for prefix,poses in [('screen',t.screen_poses),('flight',t.flight_poses),('bankai',t.bankai_poses),('waterPose',t.water_poses),('twinLionPose',t.twin_lion_poses),('facing',t.facing_poses)]:
 for frame,pose in poses.items():
  key='-'.join(map(str,frame)) if isinstance(frame,tuple) else str(frame);b[prefix+'-'+key]=pose
a.output.write_text('\n'.join(key+'\t'+'\t'.join(map(str,value)) for key,value in b.items())+'\n')
