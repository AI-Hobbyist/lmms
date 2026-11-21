# LMMS 多轨、MIDI、音频与插件路由

分析基线为本仓库 `50b746b99`。以下结论来自播放、渲染和混音源码的静态核对，覆盖本分支的 Pattern、SVS 和 SVC 轨。

## 一次播放如何到达 Master

以“两条乐器轨共用一个混响”为例。两条轨分别读取自己片段里的音符，各自的乐器实例生成采样。若两条轨的输出都选混音通道 3，把混响插在通道 3，两轨会先相加，再一起经过同一个混响实例。若混响只插在第一条轨的本地效果链，它只处理第一条轨。若要保留各轨独立的干声电平并单独调混响量，可以让两轨分别输出到两个混音通道，从各通道按不同发送量送往同一个混响通道，再由混响通道送往 Master。保留原来的直达 Master 发送即可同时听到干声。

```text
Song 时间线 / Pattern 列
  ├─ 乐器轨 → MidiClip 音符 → 音符播放句柄 → 乐器渲染 ─┐
  ├─ 采样轨 → SampleClip → 采样播放句柄 ──────────────┤
  └─ SVS / SVC 轨 → 专用人声播放句柄 ──────────────────┘
                 ↓ 各自的轨道音频总线
         轨道本地处理 → 指定的 MixerChannel
                 ↓ 通道效果链、通道音量、通道间发送
              Master（通道 0）→ 引擎输出缓冲
```

音频引擎每个缓冲周期分四阶段：准备播放句柄、渲染句柄、处理轨道总线效果、执行主混音。Song 在准备阶段按 tick 调度，给轨道传入播放位置、帧数、周期内帧偏移和片段选择。音符的音乐时间与样本在输出缓冲里的位置由此衔接。[AudioEngine](../../src/core/AudioEngine.cpp) · [Song](../../src/core/Song.cpp)

## 多轨和片段的关联

| 对象 | 关联方式 | 对播放的影响 |
|---|---|---|
| Song、Track、Clip | 轨道容器保存轨道，轨道管理片段。Song 按播放模式选择轨道与片段 | 整曲播放和单片段预听可共用轨道播放入口，调度范围不同 |
| PatternTrack、PatternStore | 编排中的 PatternTrack 定位当前 Pattern 片段，将时间换算为片段内位置，再用该轨对应的列号调用 PatternStore | PatternStore 遍历成员轨并传入同一列号。一次编排触发可同时驱动多条成员轨，各成员仍沿自身音频路径输出 |
| InstrumentTrack、MidiClip、Instrument | 片段保存音符，轨道持有乐器实例。到点的音符生成 NotePlayHandle | 同轨多个 MIDI 片段共用轨上的音色和输出通道。逐音符渲染调用乐器的 playNote，流式乐器则有 InstrumentPlayHandle 按缓冲调用 play |
| SampleTrack、SampleClip | 播放经过的采样片段通过 SamplePlayHandle 进入引擎 | 音频经采样轨自己的总线送入所选混音通道，无需乐器合成 |
| SVSTrack、SVCTrack | SVS 按区域时间映射选取已生成音频；SVC 根据片段播放状态快照定位音频 | SVS 调度带范围的播放句柄；SVC 连续播放时复用流式句柄保存重采样状态，跳转或裁剪可重新定位。两者都可指定混音通道 |

源码入口：[TrackContainer](../../src/core/TrackContainer.cpp) · [PatternTrack](../../src/tracks/PatternTrack.cpp) · [PatternStore](../../src/core/PatternStore.cpp) · [InstrumentTrack](../../src/tracks/InstrumentTrack.cpp) · [InstrumentPlayHandle](../../src/core/InstrumentPlayHandle.cpp) · [SampleTrack](../../src/tracks/SampleTrack.cpp) · [SVSTrack](../../src/tracks/SVSTrack.cpp) · [SVCTrack](../../src/tracks/SVCTrack.cpp)

## MIDI、自动化和音频分别传什么

- **实时 MIDI 输入**由 MidiPort 按输入开关与 MIDI 通道筛选。输入通道配置值 0 表示接受所有通道。音符类事件还会检查键值，并可应用固定力度；随后端口将事件交给轨道。InstrumentTrack 的音符开启路径建立播放句柄。时间线音符则由 InstrumentTrack 的 play 直接调度，所以排查“片段能响、键盘不响”应先看输入端口和通道。[MidiPort](../../src/core/midi/MidiPort.cpp) · [InstrumentTrack](../../src/tracks/InstrumentTrack.cpp)
- **外部 MIDI 输出**从 InstrumentTrack 的 processOutEvent 到 MidiPort，再由输出开关、通道、固定力度及固定键设置决定交给 MidiClient 的事件。改变轨道的 Mixer 通道只改变内部音频目的地；外接设备产生的声音要通过独立音频路径监听。[MidiPort](../../src/core/midi/MidiPort.cpp)
- **自动化**由 Song 在调度轨道之前处理，按照当前时间求出与片段关联的参数值。它改变参数后，乐器、效果或混音器在各自的处理位置消费这些值。例如自动化发送量会改变一路混音比例，自动化片段自身不生成 PCM。[Song](../../src/core/Song.cpp) · [Mixer](../../src/core/Mixer.cpp)

## 插件与混音的处理边界

1. **乐器插件生成音频。** 乐器的宿主是 InstrumentTrack，播放句柄负责当前音符或当前音频周期。MIDI 文件导入插件属于工程编辑入口，将文件转换为轨道和音符；播放导入结果时走普通乐器轨路径。[InstrumentTrack](../../src/tracks/InstrumentTrack.cpp) · [MIDI 导入器](../../plugins/MidiImport/MidiImport.cpp)
2. **轨道本地效果处理本轨汇总信号。** AudioBusHandle 汇集关联句柄，在配置了音量或声像模型时应用相应增益，然后处理本地效果，将输出交给 nextMixerChannel。排查单轨失声时，可沿句柄输出、本地效果、目标通道逐段查。[AudioBusHandle](../../src/core/AudioBusHandle.cpp)
3. **混音通道效果处理汇合后的信号。** 接收通道累加发送方缓冲时使用“发送方通道音量 × 该路发送量”，再处理自己的效果链。EffectChain 按顺序把同一缓冲交给各效果实例，因此移动效果顺序会改变下游插件的输入。[Mixer](../../src/core/Mixer.cpp) · [EffectChain](../../src/core/EffectChain.cpp)
4. **发送构成有向依赖。** 一条 MixerRoute 关联发送通道、接收通道和发送量。再次创建同一对端点的发送会更新已有发送量。接收方依据上游处理进度进入队列，最后由 masterMix 汇总。GUI 和 Agent 建立发送时都用 isInfiniteLoop 判断反馈环；底层 createChannelSend 的调用者需要区分入口校验与实际连线操作。[Mixer](../../src/core/Mixer.cpp) · [MixerView](../../src/gui/MixerView.cpp) · [Agent 混音命令](../../src/agent/CoreCommands.cpp)

排查工程关联时，可先写下三项：哪个片段触发哪个播放对象、哪个参数正在被控制、该对象的采样最终送到哪个混音通道。尤其要分别记录 **Pattern 列号、MIDI 通道号、Mixer 通道号**。三者各自选择片段、事件接收范围和音频目的地，即使数字相同也没有隐含连接。
