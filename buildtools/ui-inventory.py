"""Freeze UI scope from the approved plan and the existing generated build targets."""
from pathlib import Path
import re
import subprocess
import wave
import math
import struct
import json

root = Path(__file__).resolve().parents[1]
out = root / 'doc/ui-modernization'
(out / 'validation').mkdir(parents=True, exist_ok=True)
(out / 'fixtures').mkdir(exist_ok=True)
plan = (root / 'LMMS界面现代扁平圆角化计划书.md').read_text(encoding='utf-8')
spec = plan.split('## 3. 视觉规格')[1].split('## 4.')[0]
(out / 'visual-spec.md').write_text('# 冻结视觉规格\n\nF0：2026-10-06。按计划初始值冻结，不另设视觉系统。单位为 DIP；普通表单 28，紧凑轨道约 20，编辑工具栏 28–32。\n' + spec, encoding='utf-8')
rows=[]
for line in plan.splitlines():
    if re.match(r'\| [ABCDE]\d\d ',line):
        cells=[c.strip() for c in line.strip('|').split('|')]
        ident=cells[0]
        stage={'A':'F1','B':'F2/F3/F4','C':'F2/F3/F4','D':'F1/F5/F6','E':'F5/F6'}[ident[0]]
        if ident in ['C02','C05','C06','C07','C09','C10','C11','B02','B03']: stage='F3'
        if ident in ['C08','B04','B05','B07','B08']: stage='F4'
        scenes='S01/S02/S07' if ident.startswith('A') else 'S02–S08'
        rows.append('| '+ ' | '.join([ident,cells[1],cells[2],cells[3] if len(cells)>3 else '按计划第 7 节',stage,scenes,'TODO'])+' |')
inventory='# UI 覆盖清单\n\n基线：`146212993`，当前 Visual Studio Release 配置。A=QSS；B=已有属性；C=自绘；D=资源/布局；E=宿主边界。动作、属性、约束沿用计划对应编号；下表与插件明细共同封口。\n\n| ID | 路径/入口/页面 | 绘制、属性及动作 | 保留契约 | 阶段 | 验收 | 状态 |\n|---|---|---|---|---|---|---|\n'+'\n'.join(rows)
pluginlist=(root/'cmake/modules/PluginList.cmake').read_text()
plugins=re.search(r'SET\(LMMS_PLUGIN_LIST(.*?)\n\)',pluginlist,re.S)[1].split()
plugins=['AudioFileProcessor','Kicker','TripleOscillator']+[p for p in plugins if not p.startswith('${')]
details=[]; enabled=[]; unavailable=[]; external=[]; build_targets=[]; artifacts=[]
no_ui={'CarlaBase','VstBase','MidiImport','MidiExport','HydrogenImport'}
for plugin in plugins:
    directory=root/'plugins'/plugin
    projects=list((root/'build/plugins'/plugin).glob('*.vcxproj'))
    if plugin == 'Stk':
        directory = directory / 'Mallets'
        projects=list((root/'build/plugins/Stk/Mallets').glob('*.vcxproj'))
    targets=[p.stem for p in projects if p.stem not in ['INSTALL','PACKAGE','RUN_TESTS','ALL_BUILD']]
    if plugin in no_ui:
        external.append(plugin+'：基础桥接/文件转换，无面板；目标 '+', '.join(targets))
        continue
    if not targets:
        unavailable.append(plugin+'：当前配置没有生成 UI 构建目标')
        continue
    sources=[]; classes=set(); artwork=set(); controls=set(); geometry=[]
    for source in list(directory.glob('*.cpp'))+list(directory.glob('*.h')):
        text=source.read_text(encoding='utf-8',errors='replace')
        if not re.search(r'View|Dialog|QWidget',text): continue
        sources.append(source.relative_to(root).as_posix())
        classes.update(re.findall(r'\b(\w*(?:View|Dialog))\s*::\s*\1\s*\(',text))
        classes.update(re.findall(r'class\s+(\w*(?:View|Dialog))\b',text))
        artwork.update(re.findall(r'getIconPixmap\s*\(\s*"([^"]+)"',text))
        controls.update(re.findall(r'new\s+(Knob|Lcd\w+|LedCheckBox|PixmapButton|\w*Graph|Q\w+|TabWidget|GroupBox)\b',text))
        if re.search(r'setGeometry|move\(|setFixedSize',text): geometry.append(source.name)
    enabled.append(plugin)
    primary = {'ZynAddSubFx':'zynaddsubfx','OpulenZ':'opulenz'}.get(plugin, targets[0])
    build_targets.append(primary)
    dll=root/'build/plugins/Release'/f'{primary}.dll'
    artifacts.append({'plugin':plugin,'target':primary,'dllPresent':dll.exists(),
                      'buildStatus':'PENDING: presence does not prove a fresh successful build'})
    details.append('| '+' | '.join([plugin,', '.join(targets),' / '.join(sources),' / '.join(sorted(classes)) or 'instantiateView / createView',' / '.join(sorted(artwork)) or '无直接 artwork 引用',' / '.join(sorted(controls)) or '共享/私有 painter',', '.join(geometry) or '布局管理器/需按入口验证','F5 / S08','TODO'])+' |')
inventory+='\n\n## D03：已启用自有宿主/插件 UI（'+str(len(enabled))+' 项）\n\n每项均纳入 F5，不把第三方内容纳入换肤承诺。记录的源文件为该插件直接 UI/工厂入口，artwork 名为插件资源命名空间内的键；专有图表随同一面板验证。\n\n| 插件 | 构建目标 | 源码入口 | View/Dialog | artwork/图标键 | 共享/私有控件 | 固定几何入口 | 阶段/验收 | 状态 |\n|---|---|---|---|---|---|---|---|---|\n'+'\n'.join(details)
inventory+='\n\n## 未构建\n\n'+'\n'.join('- '+s for s in unavailable)
inventory+='\n\n## 非面板目标与外部边界\n\n'+'\n'.join('- '+s for s in external)+'\n- VST/VST3/LV2、Carla 原生编辑器、ZynAddSubFX vendored 原生 GUI、系统文件对话框与 Windows 标题栏：E01/E02，仅宿主容器。\n- LADSPA 的 CALF/CAPS/CMT/TAP DSP 目标无独立 LMMS UI；使用 LadspaEffect 通用宿主面板。\n- SVS 设置、编辑器和公共插件页面包含在 A11/B08/D02；SVSExample/AI 示例没有独立 QWidget 插件面板。\n'
current=[
 ('C01','ComboBox','LMMS 单层文字/阴影、位图箭头和矩形框','编辑工具栏、仪器 MIDI 页','F2','S02/S03'),
 ('C02','LcdWidget','数字 sprite 字符格；标签文字','主工具栏、轨道、仪器/效果','F3','S06/S08'),
 ('C03','TabWidget','自绘标题/标签矩形和固定测量','仪器公共页、机架','F2','S02/S08'),
 ('C04','GroupBox','自绘 darker 面板与可切换标题 LED','仪器声音塑形/控制器','F2','S08'),
 ('C05','Knob','Styled 弧线/指针或 legacy 位图；pixmap 缓存','轨道、仪器、Mixer、插件','F3','S06/S08'),
 ('C06','Fader','电平 painter、位图帽；位置/dB 映射','Mixer/插件推子','F3','S06'),
 ('C07','LedCheckBox','on/off 位图；PixmapButton 的共享按钮状态','仪器、控制器、插件','F3','S06/S08'),
 ('C08','ClipView','背景/文字主题属性；派生类双层矩形内容绘制','Song/Pattern/SVS 片段','F4','S03/S05'),
 ('C09','CPULoadWidget','缓存背景和 LED sprite；100ms 刷新','主工具栏','F3','S01'),
 ('C10','PianoView','仪器琴键位图；黑白键独立命中','仪器公共页','F3','S08'),
 ('C11','Graph','自绘数据、背景 artwork 和外框','包络/LFO/波形及 D03 私有图表','F3/F5','S08'),
 ('C12','LmmsStyle','Fusion 代理样式、drawPrimitive 多层边框','所有标准控件及 MDI 装饰','F2','S02/S07'),
 ('C13','SubWindow','QMdiSubWindow 框体/标题；detach/attach','全部 MDI 编辑器','F2','S07'),
]
inventory+='\n\n## 当前自绘入口与实际属性（F0 快照）\n\nA01–A11 的样式路径为 `data/themes/default/style.css`；标准页面入口为 `src/gui/modals/SetupDialog.cpp`、`src/gui/modals/ExportProjectDialog.cpp`、`src/gui/editors/svs/SVSSettingsPage.cpp`、`src/gui/SideBar.cpp`、`src/gui/tracks/TrackLabelButton.cpp`。B/D 行的明确路径及插件资源键见上表；本表补齐 C 项的现状，不把拟增加的属性当成已存在。\n\n| ID | 类/头文件 | 当前绘制 | 现有 Q_PROPERTY | 调用页面 | 阶段 | 验收 |\n|---|---|---|---|---|---|---|\n'
for ident,cls,draw,page,stage,scenes in current:
    header=root/'include'/f'{cls}.h'
    text=header.read_text(encoding='utf-8',errors='replace')
    properties=re.findall(r'Q_PROPERTY\s*\((.*?)\)',text,re.S)
    names=[' '.join(p.split()[:2]) for p in properties]
    inventory+='| '+' | '.join([ident,f'`{cls}::{"drawPrimitive" if cls=="LmmsStyle" else "paintEvent"}` / `include/{cls}.h`',draw,', '.join(names) or '无独立主题 Q_PROPERTY（palette/继承属性）',page,stage,scenes])+' |\n'
(out/'inventory.md').write_text(inventory,encoding='utf-8')
(out/'validation/F0-plugin-artifacts.json').write_text(json.dumps(artifacts,indent=2),encoding='utf-8')
(out/'validation/plugin-targets.txt').write_text('\n'.join(build_targets),encoding='utf-8')
(out/'validation/F0-workspace-before.txt').write_text(subprocess.check_output(['git','status','--short'],cwd=root).decode('utf-8'),encoding='utf-8')
with wave.open(str(out/'fixtures/tone.wav'),'wb') as wav:
    wav.setparams((1,2,48000,0,'NONE','not compressed'))
    wav.writeframes(b''.join(struct.pack('<h',int(3000*math.sin(2*math.pi*440*i/48000))) for i in range(24000)))
print(f'Frozen {len(rows)} category items, {len(enabled)} enabled UI plugins; {len(unavailable)} not built.')
