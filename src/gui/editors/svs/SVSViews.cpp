#include "SVSViews.h"
#include "SVSTrack.h"
#include "SVSClip.h"
#include "SVSParameterPanel.h"
#include "SVSCanvas.h"
#include "SVSResultStrip.h"
#include "SVSLyricEditor.h"
#include "SVSImageLoader.h"
#include "ConfigManager.h"
#include "TrackLabelButton.h"
#include "StringPairDrag.h"
#include "Knob.h"
#include "MixerChannelLcdSpinBox.h"
#include "EffectRackView.h"
#include "embed.h"
#include "Engine.h"
#include "Song.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QMouseEvent>
#include <QDropEvent>
#include <QScrollBar>
#include <QToolButton>
#include <QButtonGroup>
#include <QSignalBlocker>
#include <QInputDialog>
#include <QLineEdit>
#include <QUuid>
#include <QFileDialog>
#include <QFile>
#include <QTabWidget>
#include <QLocale>
#include <QStyle>
#include <QScrollArea>
#include <QSplitter>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <memory>
#include <cmath>
namespace lmms::gui {
SVSTrackView::SVSTrackView(SVSTrack* track,TrackContainerView* container):TrackView(track,container) {
 auto* label=new TrackLabelButton(this,getTrackSettingsWidget()); label->setObjectName("svsTrackAvatar");
 auto* avatar=new SVSImageLoader(label);
 avatar->changed=[label,avatar]{const auto image=avatar->image(); label->setIcon(image.isNull()?embed::getIconPixmap("svs_track"):QPixmap::fromImage(image)); label->setToolTip(avatar->diagnostic());};
 auto refresh=[label,track,avatar]{avatar->request(track->voice().package,track->voice().avatar,label->iconSize()*label->devicePixelRatioF());}; refresh(); connect(track,&Track::dataChanged,this,refresh);
 auto* voices=new QComboBox(getTrackSettingsWidget()); voices->setObjectName("svsVoiceSelector"); voices->addItem(tr("Select voice"));
 for(const auto& voice:svs::Registry::instance().voices()) voices->addItem(voice.name,voice.pluginId+"/"+voice.id);
 auto refreshVoice=[voices,track] { auto value=track->pluginId()+"/"+track->voiceId(); voices->setCurrentIndex(std::max(0,voices->findData(value))); }; refreshVoice(); connect(track,&Track::dataChanged,this,refreshVoice);
 connect(voices,qOverload<int>(&QComboBox::activated),this,[track,voices](int index){ auto value=voices->itemData(index).toString(); if(value.isEmpty()) return; auto split=value.lastIndexOf('/'); track->bindVoice(value.left(split),value.mid(split+1)); });
 auto* volume=new VolumeKnob(KnobType::Small17,tr("VOL"),getTrackSettingsWidget(),Knob::LabelRendering::LegacyFixedFontSize,tr("Track volume")); volume->setModel(track->volumeModel());
 auto* pan=new Knob(KnobType::Small17,tr("PAN"),getTrackSettingsWidget(),Knob::LabelRendering::LegacyFixedFontSize,tr("Panning")); pan->setModel(track->panningModel());
 auto* mix=new MixerChannelLcdSpinBox(2,getTrackSettingsWidget(),tr("Mixer channel"),this); mix->setModel(track->mixerChannelModel());
 auto* layout=new QHBoxLayout(getTrackSettingsWidget()); layout->setContentsMargins(0,0,0,0); layout->setSpacing(1); layout->addWidget(label); layout->addWidget(voices); layout->addWidget(mix); layout->addWidget(volume); layout->addWidget(pan);
 connect(label,&QToolButton::clicked,this,[this,track]{ auto* dialog=new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(tr("SVS voice and effects")); auto* body=new QVBoxLayout(dialog); auto* restore=new QPushButton(tr("Restore voice name"),dialog); body->addWidget(restore); connect(restore,&QPushButton::clicked,track,&SVSTrack::restoreVoiceName); body->addWidget(new EffectRackView(track->audioBusHandle()->effects(),dialog)); dialog->show(); });
 setAcceptDrops(true);
}
void SVSTrackView::dragEnterEvent(QDragEnterEvent* event) { if(!StringPairDrag::processDragEnterEvent(event,"svsvoice")) TrackView::dragEnterEvent(event); }
void SVSTrackView::dropEvent(QDropEvent* event) { if(StringPairDrag::decodeKey(event)=="svsvoice") { auto value=StringPairDrag::decodeValue(event); auto split=value.lastIndexOf('/'); static_cast<SVSTrack*>(getTrack())->bindVoice(value.left(split),value.mid(split+1)); event->accept(); } else TrackView::dropEvent(event); }
SVSClipView::SVSClipView(SVSClip* clip,TrackView* view):ClipView(clip,view),m_clip(clip) { connect(clip,&Clip::dataChanged,this,[this]{setToolTip(m_clip->status()); update();}); }
void SVSClipView::paintEvent(QPaintEvent*) {
 QPainter p(this); p.fillRect(rect(),isSelected()?palette().highlight():palette().button()); p.setClipRect(rect().adjusted(1,1,-1,-1));
 for(const auto& note:m_clip->notes()) { double x=(note.tick+int(m_clip->startTimeOffset()))/int(m_clip->length())*width(); double w=note.duration/int(m_clip->length())*width(); double y=height()-5-(note.pitch-36)/60*(height()-10); p.fillRect(QRectF(x,y,std::max(1.,w),2),palette().highlight().color()); }
 p.setPen(palette().text().color()); p.drawText(3,12,m_clip->name()); p.drawText(3,height()-3,m_clip->status());
}
void SVSClipView::mouseDoubleClickEvent(QMouseEvent*) { auto* editor=new SVSPianoRoll(m_clip,this); editor->setAttribute(Qt::WA_DeleteOnClose); editor->show(); }
SVSPianoRoll::SVSPianoRoll(SVSClip* clip,QWidget* parent):QDialog(parent) {
 setWindowTitle(tr("SVS Piano Roll — LMMS")); resize(1100,740); auto* layout=new QVBoxLayout(this); auto* toolbar=new QHBoxLayout; layout->addLayout(toolbar);
 auto* play=new QPushButton(tr("Play"),this); toolbar->addWidget(play); connect(play,&QPushButton::clicked,Engine::getSong(),&Song::playSong);
 auto* stop=new QPushButton(tr("Stop"),this); toolbar->addWidget(stop); connect(stop,&QPushButton::clicked,Engine::getSong(),&Song::stop);
 auto* render=new QPushButton(tr("Synthesize"),this); toolbar->addWidget(render); connect(render,&QPushButton::clicked,clip,&SVSClip::synthesize);
 auto* status=new QLabel(clip->status(),this); toolbar->addWidget(status); connect(clip,&Clip::dataChanged,this,[clip,status,render]{status->setText(clip->status());render->setEnabled(!clip->readOnly());}); render->setEnabled(!clip->readOnly()); connect(clip,&QObject::destroyed,this,&QDialog::close);
 auto* body=new QHBoxLayout; layout->addLayout(body,1);
 auto* outerGrid=new QGridLayout; body->addLayout(outerGrid,1); auto* splitter=new QSplitter(Qt::Vertical,this); splitter->setObjectName("svsEditorAreas"); outerGrid->addWidget(splitter,0,0);
 auto* noteArea=new QWidget(splitter); auto* grid=new QGridLayout(noteArea); grid->setContentsMargins(0,0,0,0); auto* canvas=new SVSCanvas(clip,noteArea); m_canvas=canvas; canvas->setThemeColors(m_colors); grid->addWidget(canvas,0,0);
 auto* parameterScroll=new QScrollArea(splitter); parameterScroll->setObjectName("svsParameterArea"); parameterScroll->setWidgetResizable(true); parameterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); auto* parameterBody=new QWidget(parameterScroll); auto* parameterLayout=new QVBoxLayout(parameterBody); parameterLayout->setContentsMargins(0,0,0,0); parameterLayout->setSpacing(2); parameterScroll->setWidget(parameterBody); splitter->setSizes({440,180});
 const auto storedSizes=clip->editorState()["areaSizes"].toArray(); if(storedSizes.size()==2) splitter->setSizes({storedSizes[0].toInt(440),storedSizes[1].toInt(180)});
 connect(splitter,&QSplitter::splitterMoved,this,[clip,splitter]{auto state=clip->editorState(); QJsonArray sizes; for(auto size:splitter->sizes()) sizes.append(size); state["areaSizes"]=sizes; clip->setEditorState(state);});
 struct Lane { QWidget* frame; SVSCanvas* canvas; QCheckBox* visible; QSpinBox* height; };
 auto lanes=std::make_shared<QMap<QString,Lane>>();
 auto* strip=new SVSResultStrip(clip,noteArea); strip->setThemeColors(m_colors); grid->addWidget(strip,1,0); connect(canvas,&SVSCanvas::viewportChanged,this,[canvas,strip]{strip->setViewport(canvas->scrollTick(),canvas->horizontalZoom());}); strip->setViewport(canvas->scrollTick(),canvas->horizontalZoom()); connect(strip,&SVSResultStrip::scrollRequested,this,[canvas](double tick){canvas->setScroll(tick,canvas->topPitch());});
 auto* horizontal=new QScrollBar(Qt::Horizontal,this); horizontal->setObjectName("svsHorizontalScroll"); outerGrid->addWidget(horizontal,1,0);
 auto* vertical=new QScrollBar(Qt::Vertical,this); vertical->setObjectName("svsVerticalScroll"); vertical->setRange(0,12700); grid->addWidget(vertical,0,1);
 auto refreshScroll=[clip,canvas,horizontal,vertical]{ QSignalBlocker h(horizontal),v(vertical); horizontal->setRange(0,std::max(19200,int(clip->length())*100)); horizontal->setValue(int(canvas->scrollTick()*100)); vertical->setValue(int((127-canvas->topPitch())*100)); };
 connect(canvas,&SVSCanvas::viewportChanged,this,refreshScroll); connect(clip,&Clip::dataChanged,this,refreshScroll); refreshScroll();
 connect(horizontal,&QScrollBar::valueChanged,this,[canvas](int value){canvas->setScroll(value/100.,canvas->topPitch());});
 connect(vertical,&QScrollBar::valueChanged,this,[canvas](int value){canvas->setScroll(canvas->scrollTick(),127-value/100.);});
 auto* tools=new QButtonGroup(this);
 canvas->batchLyricsRequested=[this,clip,canvas]{auto* dialog=new SVSLyricEditor(clip,canvas->selectedNotes(),this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->open();};
 auto* lyrics=new QToolButton(this); lyrics->setObjectName("svsBatchLyricsButton"); lyrics->setText(tr("Lyrics")); toolbar->addWidget(lyrics); connect(lyrics,&QToolButton::clicked,this,[canvas]{if(canvas->batchLyricsRequested) canvas->batchLyricsRequested();});
 const QStringList toolNames{tr("Notes"),tr("Freehand"),tr("Anchor"),tr("Line"),tr("Smooth"),tr("Erase")};
 for(int i=0;i<toolNames.size();++i) { auto* button=new QToolButton(this); button->setObjectName(QString("svsTool%1").arg(i)); button->setText(toolNames[i]); button->setCheckable(true); button->setChecked(i==0); tools->addButton(button,i); toolbar->addWidget(button); }
 connect(tools,&QButtonGroup::idClicked,this,[canvas,lanes,strip](int id){strip->cancelOperation(); canvas->setTool(static_cast<SVSCanvas::Tool>(id)); for(const auto& lane:*lanes) lane.canvas->setTool(static_cast<SVSCanvas::Tool>(id));});
 auto* quantization=new QComboBox(this); quantization->setObjectName("svsQuantization");
 for(int divisor:{1,2,4,8,16,32,64}) quantization->addItem(QString("1/%1").arg(divisor),double(TimePos::ticksPerBar())/divisor);
 quantization->setCurrentIndex(std::max(0,quantization->findData(clip->editorState()["quantization"].toDouble(12)))); toolbar->addWidget(quantization);
 connect(quantization,qOverload<int>(&QComboBox::activated),this,[canvas,quantization](int index){canvas->setQuantization(quantization->itemData(index).toDouble());});
 auto* zoomOut=new QToolButton(this); zoomOut->setText(tr("−")); toolbar->addWidget(zoomOut);
 auto* zoomIn=new QToolButton(this); zoomIn->setText(tr("+")); toolbar->addWidget(zoomIn);
 connect(zoomOut,&QToolButton::clicked,this,[canvas]{canvas->setZoom(canvas->horizontalZoom()/1.25,canvas->verticalZoom());});
 connect(zoomIn,&QToolButton::clicked,this,[canvas]{canvas->setZoom(canvas->horizontalZoom()*1.25,canvas->verticalZoom());});
 toolbar->addStretch();
 auto* track=static_cast<SVSTrack*>(clip->getTrack()); auto* side=new QWidget(this); side->setObjectName("svsVoicePanel"); side->setMaximumWidth(300); auto* controls=new QVBoxLayout(side); body->addWidget(side);
 auto* properties=new QToolButton(this); properties->setText(tr("Properties")); properties->setCheckable(true); properties->setChecked(true); toolbar->addWidget(properties); connect(properties,&QToolButton::toggled,side,&QWidget::setVisible);
 auto* portrait=new SVSImageLoader(canvas); portrait->setObjectName("svsPortraitLoader"); auto* portraitVisible=new QCheckBox(tr("Show portrait"),side); portraitVisible->setObjectName("svsPortraitVisible"); controls->addWidget(portraitVisible);
 auto* portraitRow=new QHBoxLayout; controls->addLayout(portraitRow); portraitRow->addWidget(new QLabel(tr("Transparency"),side)); auto* transparency=new QSlider(Qt::Horizontal,side); transparency->setObjectName("svsPortraitTransparency"); transparency->setRange(0,100); portraitRow->addWidget(transparency,1);
 auto* transparencyValue=new QSpinBox(side); transparencyValue->setObjectName("svsPortraitTransparencyValue"); transparencyValue->setRange(0,100); transparencyValue->setSuffix("%"); portraitRow->addWidget(transparencyValue);
 auto* portraitDiagnostic=new QLabel(side); portraitDiagnostic->setObjectName("svsPortraitDiagnostic"); portraitDiagnostic->setWordWrap(true); controls->addWidget(portraitDiagnostic);
 auto applyPortrait=[track=QPointer<SVSTrack>(track),canvas,portrait,portraitVisible,transparency,transparencyValue,portraitDiagnostic]{
  if(!track) return;
  const auto settings=track->portraitSettings(); {QSignalBlocker v(portraitVisible),s(transparency),n(transparencyValue); portraitVisible->setChecked(settings["visible"].toBool(true)); transparency->setValue(settings["transparency"].toInt(70)); transparencyValue->setValue(transparency->value());}
  canvas->setPortrait(portrait->image(),portraitVisible->isChecked(),transparency->value(),{settings["x"].toDouble(1),settings["y"].toDouble(1)}); portraitDiagnostic->setText(portrait->diagnostic()); portraitDiagnostic->setVisible(!portrait->diagnostic().isEmpty());
 };
 portrait->changed=applyPortrait; auto refreshPortrait=[track=QPointer<SVSTrack>(track),canvas,portrait,applyPortrait]{if(!track) return; portrait->request(track->voice().package,track->voice().portrait,canvas->portraitTargetSize()); applyPortrait();};
 connect(track,&Track::dataChanged,this,refreshPortrait); connect(canvas,&SVSCanvas::portraitSizeChanged,this,refreshPortrait); refreshPortrait();
 connect(portraitVisible,&QCheckBox::toggled,this,[track](bool visible){auto settings=track->portraitSettings(); settings["visible"]=visible; track->setPortraitSettings(settings);});
 auto setTransparency=[track](int value){auto settings=track->portraitSettings(); settings["transparency"]=value; track->setPortraitSettings(settings);}; connect(transparency,&QSlider::valueChanged,this,setTransparency); connect(transparencyValue,qOverload<int>(&QSpinBox::valueChanged),this,setTransparency);
 auto* portraitButtons=new QHBoxLayout; controls->addLayout(portraitButtons); auto* resetPortrait=new QPushButton(tr("Reset portrait"),side); resetPortrait->setObjectName("svsPortraitReset"); portraitButtons->addWidget(resetPortrait); connect(resetPortrait,&QPushButton::clicked,this,[track]{track->setPortraitSettings({{"visible",true},{"transparency",70},{"x",1.},{"y",1.}});});
 auto* defaultPortrait=new QPushButton(tr("Use for new tracks"),side); defaultPortrait->setObjectName("svsPortraitDefaults"); portraitButtons->addWidget(defaultPortrait); connect(defaultPortrait,&QPushButton::clicked,this,[track]{const auto settings=track->portraitSettings(); ConfigManager::inst()->setValue("svs","portraitVisible",settings["visible"].toBool()?"1":"0"); ConfigManager::inst()->setValue("svs","portraitTransparency",QString::number(settings["transparency"].toInt(70)));});
 auto* moveCurves=new QCheckBox(tr("Move curves with notes"),side); moveCurves->setObjectName("svsMoveCurvesWithNotes"); moveCurves->setChecked(clip->editorState()["moveCurves"].toBool()); canvas->setMoveCurves(moveCurves->isChecked()); controls->addWidget(moveCurves); connect(moveCurves,&QCheckBox::toggled,this,[canvas,clip](bool value){canvas->setMoveCurves(value); auto state=clip->editorState(); state["moveCurves"]=value; clip->setEditorState(state);});
 auto* language=new QComboBox(side); language->setObjectName("svsLanguage"); controls->addWidget(language);
 connect(language,qOverload<int>(&QComboBox::activated),this,[track,language](int index){ track->setLanguage(language->itemData(index).toString()); });
 auto* diagnostics=new QLabel(side); diagnostics->setWordWrap(true); controls->addWidget(diagnostics);
 auto* import=new QPushButton(tr("Import project dictionary"),side); controls->addWidget(import);
 connect(import,&QPushButton::clicked,this,[this,clip,diagnostics]{ auto path=QFileDialog::getOpenFileName(this,tr("Import dictionary"),{},tr("JSON dictionary (*.json)")); if(path.isEmpty()) return; QFile file(path); if(!file.open(QIODevice::ReadOnly)) { diagnostics->setText(file.errorString()); return; } QString error; if(!clip->importDictionary(file.read(4*1024*1024+1),error)) diagnostics->setText(error); });
 auto* tabs=new QTabWidget(side); controls->addWidget(tabs);
 auto* trackPanel=new SVSParameterPanel(tabs); tabs->addTab(trackPanel,tr("Track"));
 auto* clipPanel=new SVSParameterPanel(tabs); tabs->addTab(clipPanel,tr("Clip"));
 auto* notePanel=new SVSParameterPanel(tabs); tabs->addTab(notePanel,tr("Notes"));
 auto* phonemePanel=new SVSParameterPanel(tabs); tabs->addTab(phonemePanel,tr("Phoneme"));
 auto refreshPhoneme=[clip,track,strip,phonemePanel]{ auto context=track->parameters(); for(auto i=clip->parameters().begin();i!=clip->parameters().end();++i) context[i.key()]=i.value(); phonemePanel->refresh(track->capabilities().parameters,"phoneme",{strip->selectedParameters()},context,[strip](const QString& id,const QJsonValue& value){strip->setSelectedParameter(id,value);}); phonemePanel->setEnabled(strip->selectedPhoneme()>=0&&track->capabilities().original["phonemes"].toObject()["attributesEditable"].toBool()); };
 connect(strip,&SVSResultStrip::selectionChanged,this,refreshPhoneme); connect(clip,&Clip::dataChanged,this,refreshPhoneme); connect(track,&Track::dataChanged,this,refreshPhoneme); refreshPhoneme();
 auto* feedbackPanel=new SVSParameterPanel(tabs); tabs->addTab(feedbackPanel,tr("Result"));
 auto refresh=[this,clip,track,canvas,lanes,parameterBody,parameterLayout,language,diagnostics,trackPanel,clipPanel,notePanel,feedbackPanel]{
  auto context=track->parameters(); for(const auto& p:track->capabilities().parameters) if(p.scope=="track"&&!context.contains(p.id)) context[p.id]=p.defaultValue;
  for(auto i=clip->parameters().begin();i!=clip->parameters().end();++i) context[i.key()]=i.value();
  trackPanel->refresh(track->capabilities().parameters,"track",{track->parameters()},context,[track](const QString& id,const QJsonValue& value){track->setParameter(id,value);});
  clipPanel->refresh(track->capabilities().parameters,"clip",{clip->parameters()},context,[clip](const QString& id,const QJsonValue& value){clip->setParameter(id,value);});
  QVector<QJsonObject> selected; QStringList selectedIds; for(const auto& note:clip->notes()) if(canvas->selectedNotes().contains(note.id)) { selected<<note.parameters; selectedIds<<note.id; }
  notePanel->refresh(track->capabilities().parameters,"note",selected,context,[clip,selectedIds](const QString& id,const QJsonValue& value){clip->setNoteParameter(selectedIds,id,value);}); notePanel->setEnabled(!selected.isEmpty());
  auto audio=clip->audio(); feedbackPanel->refresh(track->capabilities().feedbackParameters,"clip",audio?QVector<QJsonObject>{audio->feedback["parameters"].toObject()}:QVector<QJsonObject>{},context,{});
  QStringList current; for(int i=0;i<language->count();++i) current<<language->itemData(i).toString();
  if(current!=track->capabilities().languages) { language->clear(); for(const auto& value:track->capabilities().languages) { auto label=QLocale(value).nativeLanguageName(); language->addItem(label.isEmpty()?value:label,value); } }
  language->setCurrentIndex(language->findData(track->language())); diagnostics->setText(track->capabilityDiagnostics().join('\n'));
  QSet<QString> shown;
  auto configureLane=[&](const svs::Parameter& parameter,bool feedback) {
   if(!parameter.curve||parameter.type=="string") return;
   const auto key=(feedback?"feedback:":"input:")+parameter.id;
   if(!lanes->contains(key)) {
    auto* frame=new QWidget(parameterBody); frame->setObjectName("svsParameterTrack."+key); auto* box=new QVBoxLayout(frame); box->setContentsMargins(0,0,0,0); auto* header=new QHBoxLayout; box->addLayout(header);
    auto* visible=new QCheckBox(frame); visible->setObjectName("svsParameterVisible."+key); auto* height=new QSpinBox(frame); height->setObjectName("svsParameterHeight."+key); height->setRange(80,320); height->setSuffix(tr(" px")); header->addWidget(visible,1); header->addWidget(height);
    auto* lane=new SVSCanvas(clip,frame); lane->setParameterLane(parameter,feedback); lane->setThemeColors(m_colors); lane->setTool(canvas->tool()); box->addWidget(lane); parameterLayout->addWidget(frame); lanes->insert(key,{frame,lane,visible,height});
    connect(visible,&QCheckBox::toggled,this,[clip,lane,key](bool value){lane->setVisible(value); auto state=clip->editorState(); auto states=state["lanes"].toObject(); auto settings=states[key].toObject(); settings["visible"]=value; states[key]=settings; state["lanes"]=states; clip->setEditorState(state);});
    connect(height,qOverload<int>(&QSpinBox::valueChanged),this,[clip,lane,key](int value){lane->setFixedHeight(value); auto state=clip->editorState(); auto states=state["lanes"].toObject(); auto settings=states[key].toObject(); settings["height"]=value; states[key]=settings; state["lanes"]=states; clip->setEditorState(state);});
    connect(canvas,&SVSCanvas::viewportChanged,this,[canvas,lane]{if(lane->horizontalZoom()!=canvas->horizontalZoom()) lane->setZoom(canvas->horizontalZoom(),lane->verticalZoom()); if(lane->scrollTick()!=canvas->scrollTick()) lane->setScroll(canvas->scrollTick(),lane->topPitch());});
    connect(lane,&SVSCanvas::viewportChanged,this,[canvas,lane]{if(canvas->horizontalZoom()!=lane->horizontalZoom()) canvas->setZoom(lane->horizontalZoom(),canvas->verticalZoom()); if(canvas->scrollTick()!=lane->scrollTick()) canvas->setScroll(lane->scrollTick(),canvas->topPitch());});
   }
   auto lane=lanes->value(key); lane.canvas->setParameterLane(parameter,feedback); lane.visible->setText((feedback?tr("Result: "):QString{})+parameter.name); lane.frame->setToolTip(parameter.disabledReason);
   const auto settings=clip->editorState()["lanes"].toObject()[key].toObject(); { QSignalBlocker v(lane.visible),h(lane.height); lane.visible->setChecked(settings["visible"].toBool(true)); lane.height->setValue(settings["height"].toInt(110)); }
   lane.canvas->setFixedHeight(lane.height->value()); lane.canvas->setVisible(lane.visible->isChecked()); lane.frame->setVisible(parameter.isVisible(context));
   shown.insert(key);
  };
  for(const auto& parameter:track->capabilities().parameters) configureLane(parameter,false);
  for(const auto& parameter:track->capabilities().feedbackParameters) configureLane(parameter,true);
  for(auto i=lanes->begin();i!=lanes->end();++i) if(!shown.contains(i.key())) i->frame->hide();
 };
 connect(clip,&Clip::dataChanged,this,refresh); connect(track,&Track::dataChanged,this,refresh); connect(canvas,&SVSCanvas::selectionChanged,this,refresh); refresh();
 auto migrationState=[clip,track,canvas,strip,parameterBody,tabs,lyrics,language,import]{const bool editable=!clip->readOnly();canvas->setEnabled(editable);strip->setEnabled(editable);parameterBody->setEnabled(editable);tabs->setEnabled(editable);lyrics->setEnabled(editable);language->setEnabled(!track->readOnly());import->setEnabled(editable);};
 connect(clip,&Clip::dataChanged,this,migrationState);connect(track,&Track::dataChanged,this,migrationState);migrationState();
}
void SVSPianoRoll::setThemeColor(const QString& name,const QColor& value) {
 if(value.isValid()) m_colors[name]=value; else m_colors.remove(name);
 for(auto* canvas:findChildren<SVSCanvas*>()) canvas->setThemeColors(m_colors);
 for(auto* strip:findChildren<SVSResultStrip*>()) strip->setThemeColors(m_colors);
}
void SVSPianoRoll::changeEvent(QEvent* event) {
 QDialog::changeEvent(event);
 if(event->type()==QEvent::StyleChange&&!m_polishingTheme) {
  m_polishingTheme=true;
  m_colors.clear();
  style()->unpolish(this); style()->polish(this);
  m_polishingTheme=false;
 }
 if(event->type()==QEvent::PaletteChange||event->type()==QEvent::StyleChange) for(auto* canvas:findChildren<SVSCanvas*>()) canvas->setThemeColors(m_colors);
 if(event->type()==QEvent::PaletteChange||event->type()==QEvent::StyleChange) for(auto* strip:findChildren<SVSResultStrip*>()) strip->setThemeColors(m_colors);
}
}
