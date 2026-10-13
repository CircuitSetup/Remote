"""Embed the controls fragment and script for the existing portal pages."""
from pathlib import Path
import gzip
import json

def embed(root):
    root=Path(root)
    html=(root/'src/remote_config.html').read_text(encoding='utf-8')
    script=(root/'src/remote_config.js').read_text(encoding='utf-8')
    source=('const remoteControls=document.getElementById("remote-controls");\n'
            'const settingsForm=remoteControls.closest("form");\n'
            'if(settingsForm){settingsForm.after(remoteControls);if(settingsForm.getAttribute("action")==="/param3save"){'
            'const save=settingsForm.querySelector("button[type=submit]");if(save){'
            'if(!settingsForm.id)settingsForm.id="elrs-settings";save.setAttribute("form",settingsForm.id);remoteControls.after(save);}}}\n'
            'remoteControls.innerHTML='+json.dumps(html,ensure_ascii=False)+';\n'+script).encode('utf-8')
    data=gzip.compress(source,compresslevel=9,mtime=0)
    lines=['#pragma once','#include <stdint.h>','static const uint8_t remoteConfigUiGzip[] PROGMEM = {']
    lines+=['    '+','.join(f'0x{x:02x}'for x in data[i:i+16])+','for i in range(0,len(data),16)]
    lines+=['};','static constexpr size_t remoteConfigUiGzipLength=sizeof(remoteConfigUiGzip);','']
    header=root/'src/remote_config_ui.h';text='\n'.join(lines)
    if not header.exists() or header.read_text()!=text:header.write_text(text,encoding='utf-8')
    return len(source),len(data)

if __name__=='__main__':print('portal source/gzip bytes:',*embed(Path(__file__).resolve().parents[1]))
