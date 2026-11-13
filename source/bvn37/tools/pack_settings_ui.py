#!/usr/bin/env python3
"""Render the original Chinese setting labels for PSP controls."""
import argparse,hashlib,json
from pathlib import Path
from pack_versus_ui import render_label,pack_images,training_highlight
def build(reference,output,font):
 path=reference/'upstream/shared/assets/assets/config/language/zh-CN.json';raw=json.loads(path.read_text())['txt'];labels=raw['set_btn_group'];keys=raw['set_ctrl_btn_ui']
 rows=['com_level','operate_mode','life','time','sound','bgm','quality','show_hp']
 values=[[labels['com_level_'+v]for v in ['very_easy','easy','normal','hard','very_hard','hell']],
  [labels['operate_normal'],labels['operate_classic']],['50%','100%','200%','300%','500%'],
  [labels['time_'+v]for v in ['30','60','90','unlimited']],['0%','10%','30%','50%','70%','100%'],['0%','10%','30%','50%','70%','100%'],
  [labels['quality_'+v]for v in ['low','medium','high','best']],[labels['hp_hide'],labels['hp_show']]]
 text={'title':'设置','profile-0':labels['p1_key_set'],'profile-1':labels['p2_key_set'],'default':'恢复默认','apply':'应用','cancel':'取消','set-all':raw['common']['set_all'],
  'main-hint':'↑ / ↓ 选择  ← / → 变更  ○ 保存并返回','keys-hint':'方向键 选择或变更  × 录入按键  ○ 取消',
  'capture-hint':'按要绑定的按键  START 取消','save-error':'保存失败  × 重试  ○ 返回'}
 text.update({f'row-{i}':labels[v]for i,v in enumerate(rows)})
 text.update({f'value-{i}-{j}':v for i,row in enumerate(values)for j,v in enumerate(row)})
 text.update({f'key-{i}':keys['key_'+v]for i,v in enumerate(['up','down','left','right','attack','jump','dash','skill','super_kill','special'])})
 text.update({f'button-{i}':v for i,v in enumerate(['↑','↓','←','→','□','×','L','○','R','△'])})
 images={key:render_label(value,font,29 if 'hint'not in key and key!='save-error'else 25)for key,value in text.items()}
 images['input-plus']=render_label('＋',font,29)
 for i in range(10):images[f'button-hi-{i}']=training_highlight(images[f'button-{i}'])
 for c in '0123456789.e+-':images['hp-'+str(ord(c))]=render_label(c,font,21)
 pack_images(images,output)
 proof={'source_language':str(path),'source_language_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'row_keys':rows,'values':values,'labels':text,'font':font,'output_sha256':hashlib.sha256(output.read_bytes()).hexdigest()}
 output.with_suffix('.json').write_text(json.dumps(proof,ensure_ascii=False,indent=2)+'\n');print('Packed',len(images),'setting sprites')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('reference',type=Path);p.add_argument('output',type=Path);p.add_argument('--font',required=True);a=p.parse_args();build(a.reference.resolve(),a.output,a.font)
