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
#include "GuiApplication.h"
#include "MainWindow.h"
#include "SubWindow.h"
#include <QDialog>
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
#include <QStyleOptionToolButton>
#include <QScrollArea>
#include <QSplitter>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <memory>
#include <cmath>
namespace lmms::gui {
namespace {
// Global base values are declared by the selected engine/voice, never named here.
class SVSGlobalControls final : public QWidget {
 struct Row {QWidget* body;QLabel* label;QSlider* slider;QDoubleSpinBox* value;svs::Parameter parameter;};
 QMap<QString,Row> m_rows;QVBoxLayout* m_layout;bool m_refreshing=false;
 std::function<void(const svs::Parameter&,double)> m_setter;
 static double sliderValue(const svs::Parameter& p,int position) {
  const double t=position/10000.;const auto raw=p.scale=="log"&&p.minimum>0?p.minimum*std::pow(p.maximum/p.minimum,t):p.minimum+(p.maximum-p.minimum)*t;
  return std::clamp(p.minimum+std::round((raw-p.minimum)/p.step)*p.step,p.minimum,p.maximum);
 }
public:
 explicit SVSGlobalControls(QWidget* parent):QWidget(parent),m_layout(new QVBoxLayout(this)) {setObjectName("svsGlobalControls");m_layout->setContentsMargins(0,0,0,0);}
 void refresh(const QVector<svs::Parameter>& parameters,const QJsonObject& trackValues,const QJsonObject& clipValues,const QJsonObject& context,std::function<void(const svs::Parameter&,double)> setter) {
  m_refreshing=true;m_setter=std::move(setter);QSet<QString> present;int order=0;
  auto sorted=parameters;std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.group==b.group?a.order<b.order:a.group<b.group;});
  for(const auto& p:sorted) {
   if((p.scope!="track"&&p.scope!="clip")||(p.type!="float"&&p.type!="int")||!p.isVisible(context)) continue;
   const auto key=p.scope+"."+p.id;present.insert(key);
   if(!m_rows.contains(key)) {
    auto* body=new QWidget(this);auto* layout=new QVBoxLayout(body);layout->setContentsMargins(0,6,0,6);auto* label=new QLabel(body);layout->addWidget(label);auto* line=new QHBoxLayout;layout->addLayout(line);
    auto* slider=new QSlider(Qt::Horizontal,body);slider->setRange(0,10000);slider->setTracking(false);slider->setObjectName("svsGlobalSlider."+key);line->addWidget(slider,1);
    auto* value=new QDoubleSpinBox(body);value->setKeyboardTracking(false);value->setObjectName("svsGlobalValue."+key);value->setMaximumWidth(100);line->addWidget(value);
    m_rows.insert(key,{body,label,slider,value,p});
    connect(slider,&QSlider::valueChanged,this,[this,key](int position){if(m_refreshing) return;const auto p=m_rows[key].parameter;const double absolute=sliderValue(p,position);{QSignalBlocker block(m_rows[key].value);m_rows[key].value->setValue(absolute-p.defaultValue.toDouble());}if(m_setter) m_setter(p,absolute);});
    connect(value,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,key](double offset){if(m_refreshing) return;const auto p=m_rows[key].parameter;if(m_setter) m_setter(p,std::clamp(p.defaultValue.toDouble()+offset,p.minimum,p.maximum));});
   }
   auto& row=m_rows[key];row.parameter=p;row.label->setText(p.name);row.body->setVisible(true);row.body->setEnabled(p.enabled&&p.writable);row.body->setToolTip(p.disabledReason);m_layout->removeWidget(row.body);m_layout->insertWidget(order++,row.body);
   const auto values=p.scope=="track"?trackValues:clipValues;const auto absolute=values.value(p.id).toDouble(p.defaultValue.toDouble());const auto base=p.defaultValue.toDouble();
   QSignalBlocker sliderBlock(row.slider),valueBlock(row.value);auto precision=[](double number){auto text=QString::number(number,'f',6);while(text.endsWith('0')) text.chop(1);return std::max(0,int(text.size()-text.indexOf('.')-1));};row.value->setDecimals(p.type=="int"?0:std::max({precision(p.step),precision(base),precision(p.minimum),precision(p.maximum)}));row.value->setRange(p.minimum-base,p.maximum-base);row.value->setSingleStep(p.step);row.value->setSuffix(p.unit.isEmpty()?QString{}:" "+p.unit);row.value->setValue(absolute-base);
   const double fraction=p.maximum==p.minimum?0:p.scale=="log"&&p.minimum>0?std::log(std::clamp(absolute,p.minimum,p.maximum)/p.minimum)/std::log(p.maximum/p.minimum):(absolute-p.minimum)/(p.maximum-p.minimum);row.slider->setValue(int(std::round(fraction*10000)));
  }
  for(auto i=m_rows.begin();i!=m_rows.end();++i) if(!present.contains(i.key())) i->body->hide();m_refreshing=false;
 }
};
// TuneLab parameter tabs: left click edits, right click toggles an overlay.
class SVSParameterTab final : public QToolButton {
public:
 using QToolButton::QToolButton;
 std::function<void()> toggleVisibility;
 QColor curveColor;
 bool curveVisible=true;
 void displayState(bool editing,bool visible) {setChecked(editing);curveVisible=visible;update();}

protected:
 void mousePressEvent(QMouseEvent* event) override {
  if(event->button()==Qt::RightButton) {if(toggleVisibility) toggleVisibility();event->accept();return;}
  QToolButton::mousePressEvent(event);
 }
 void paintEvent(QPaintEvent*) override {
  QPainter painter(this);QStyleOptionToolButton option;initStyleOption(&option);option.text.clear();option.icon=QIcon{};
  style()->drawComplexControl(QStyle::CC_ToolButton,&option,&painter,this);
  const auto box=rect().adjusted(1,1,-1,-1);auto fill=curveColor;fill.setAlpha(isChecked()?90:curveVisible?35:10);
  painter.setRenderHint(QPainter::Antialiasing);painter.setBrush(fill);painter.setPen(QPen(isChecked()?curveColor:palette().color(QPalette::Mid),isChecked()?2:1));painter.drawRoundedRect(box,3,3);
  painter.setPen(isChecked()||curveVisible?curveColor:palette().color(QPalette::Disabled,QPalette::Text));painter.drawText(rect().adjusted(8,0,-8,0),Qt::AlignCenter,text());
 }

};
}

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
void SVSClipView::mouseDoubleClickEvent(QMouseEvent*) {
 if(!getGUI()) return;
 if(!m_editor) m_editor=new SVSPianoRoll(m_clip);
 m_editor->openIn(getGUI()->mainWindow());
}
void SVSPianoRoll::openIn(MainWindow* mainWindow) {
 if(!m_subWindow) {
  m_subWindow=mainWindow->addWindowedWidget(this,Qt::WindowTitleHint|Qt::WindowSystemMenuHint|Qt::WindowMinMaxButtonsHint);
  installEventFilter(this);
  m_subWindow->resize(size()+QSize(2*m_subWindow->frameWidth(),m_subWindow->titleBarHeight()+m_subWindow->frameWidth()));
 }
 m_subWindow->show();
 mainWindow->workspace()->setActiveSubWindow(m_subWindow);
 if(m_subWindow->isDetached()) {raise();activateWindow();}
 else m_subWindow->raise();
}
SVSPianoRoll::SVSPianoRoll(SVSClip* clip,QWidget* parent):QWidget(parent) {
 setWindowIcon(embed::getIconPixmap("piano"));
 setWindowTitle(tr("SVS Piano Roll — LMMS")); resize(1100,740); auto* layout=new QVBoxLayout(this); auto* toolbar=new QHBoxLayout; layout->addLayout(toolbar);
 auto* play=new QPushButton(tr("Play"),this); toolbar->addWidget(play); connect(play,&QPushButton::clicked,Engine::getSong(),&Song::playSong);
 auto* stop=new QPushButton(tr("Stop"),this); toolbar->addWidget(stop); connect(stop,&QPushButton::clicked,Engine::getSong(),&Song::stop);
 auto* render=new QPushButton(tr("Synthesize"),this); toolbar->addWidget(render); connect(render,&QPushButton::clicked,clip,&SVSClip::synthesize);
 auto* status=new QLabel(clip->status(),this); toolbar->addWidget(status); connect(clip,&Clip::dataChanged,this,[clip,status,render]{status->setText(clip->status());render->setEnabled(!clip->readOnly());}); render->setEnabled(!clip->readOnly()); connect(clip,&QObject::destroyed,this,[this]{if(m_subWindow) deleteLater();else close();});
 auto* body=new QHBoxLayout; layout->addLayout(body,1);
 auto* outerGrid=new QGridLayout; body->addLayout(outerGrid,1); auto* splitter=new QSplitter(Qt::Vertical,this); splitter->setObjectName("svsEditorAreas"); outerGrid->addWidget(splitter,0,0);
 auto* noteArea=new QWidget(splitter); auto* grid=new QGridLayout(noteArea); grid->setContentsMargins(0,0,0,0); auto* canvas=new SVSCanvas(clip,noteArea); m_canvas=canvas; canvas->setThemeColors(m_colors); grid->addWidget(canvas,0,0);
 auto* parameterBody=new QWidget(splitter); parameterBody->setObjectName("svsParameterArea");
 auto* parameterLayout=new QVBoxLayout(parameterBody);parameterLayout->setContentsMargins(0,0,0,0);parameterLayout->setSpacing(0);
 auto* parameterCanvas=new SVSCanvas(clip,parameterBody);parameterCanvas->setThemeColors(m_colors);svs::Parameter emptyParameter;parameterCanvas->setParameterLane(emptyParameter);parameterCanvas->setParameterActive(false);parameterCanvas->setMinimumHeight(80);parameterLayout->addWidget(parameterCanvas,1);
 auto* parameterScroll=new QScrollArea(parameterBody);parameterScroll->setObjectName("svsParameterTabs");parameterScroll->setWidgetResizable(true);parameterScroll->setFrameShape(QFrame::NoFrame);parameterScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);parameterScroll->setFixedHeight(44);
 auto* tabBody=new QWidget(parameterScroll);auto* parameterTabs=new QHBoxLayout(tabBody);parameterTabs->setContentsMargins(8,2,8,2);parameterTabs->setSpacing(6);parameterTabs->addStretch();parameterTabs->addStretch();parameterScroll->setWidget(tabBody);parameterLayout->addWidget(parameterScroll);
 splitter->setSizes({440,180});
 const auto storedSizes=clip->editorState()["areaSizes"].toArray();if(storedSizes.size()==2) splitter->setSizes({storedSizes[0].toInt(440),storedSizes[1].toInt(180)});
 connect(splitter,&QSplitter::splitterMoved,this,[clip,splitter]{auto state=clip->editorState();QJsonArray sizes;for(auto size:splitter->sizes()) sizes.append(size);state["areaSizes"]=sizes;clip->setEditorState(state);});
 struct Lane { SVSParameterTab* tab; svs::Parameter parameter; bool feedback; bool available=false; };
 auto lanes=std::make_shared<QMap<QString,Lane>>();
 auto selectedParameter=std::make_shared<QString>(clip->editorState()["selectedParameter"].toString());
 auto updateParameterDisplay=[clip,parameterCanvas,lanes,selectedParameter]{
  QVector<QPair<svs::Parameter,bool>> overlays;bool active=false;
  const auto states=clip->editorState()["lanes"].toObject();
  for(auto i=lanes->begin();i!=lanes->end();++i) {
   auto& lane=i.value();const bool selected=lane.available&&i.key()==*selectedParameter;const bool visible=states[i.key()].toObject()["visible"].toBool(true);
   lane.tab->displayState(selected,visible);lane.tab->setVisible(lane.available);
   if(selected) {parameterCanvas->setParameterLane(lane.parameter,lane.feedback);active=true;}
   else if(lane.available&&visible) overlays.append({lane.parameter,lane.feedback});
  }
  parameterCanvas->setParameterActive(active);parameterCanvas->setParameterOverlays(overlays);
 };
 connect(canvas,&SVSCanvas::viewportChanged,this,[canvas,parameterCanvas]{if(parameterCanvas->horizontalZoom()!=canvas->horizontalZoom()) parameterCanvas->setZoom(canvas->horizontalZoom(),parameterCanvas->verticalZoom());if(parameterCanvas->scrollTick()!=canvas->scrollTick()) parameterCanvas->setScroll(canvas->scrollTick(),parameterCanvas->topPitch());});
 connect(parameterCanvas,&SVSCanvas::viewportChanged,this,[canvas,parameterCanvas]{if(canvas->horizontalZoom()!=parameterCanvas->horizontalZoom()) canvas->setZoom(parameterCanvas->horizontalZoom(),canvas->verticalZoom());if(canvas->scrollTick()!=parameterCanvas->scrollTick()) canvas->setScroll(parameterCanvas->scrollTick(),canvas->topPitch());});
 parameterCanvas->setZoom(canvas->horizontalZoom(),parameterCanvas->verticalZoom());parameterCanvas->setScroll(canvas->scrollTick(),parameterCanvas->topPitch());
 auto* strip=new SVSResultStrip(clip,noteArea); strip->setThemeColors(m_colors); grid->addWidget(strip,1,0);grid->setRowStretch(0,1);grid->setRowStretch(1,0); connect(canvas,&SVSCanvas::viewportChanged,this,[canvas,strip]{strip->setViewport(canvas->scrollTick(),canvas->horizontalZoom());}); strip->setViewport(canvas->scrollTick(),canvas->horizontalZoom()); connect(strip,&SVSResultStrip::scrollRequested,this,[canvas](double tick){canvas->setScroll(tick,canvas->topPitch());});
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
 connect(tools,&QButtonGroup::idClicked,this,[canvas,parameterCanvas,strip](int id){strip->cancelOperation();canvas->setTool(static_cast<SVSCanvas::Tool>(id));parameterCanvas->setTool(static_cast<SVSCanvas::Tool>(id));});
 auto* quantization=new QComboBox(this); quantization->setObjectName("svsQuantization");
 for(int divisor:{1,2,4,8,16,32,64}) quantization->addItem(QString("1/%1").arg(divisor),double(TimePos::ticksPerBar())/divisor);
 quantization->setCurrentIndex(std::max(0,quantization->findData(clip->editorState()["quantization"].toDouble(12)))); toolbar->addWidget(quantization);
 connect(quantization,qOverload<int>(&QComboBox::activated),this,[canvas,quantization](int index){canvas->setQuantization(quantization->itemData(index).toDouble());});
 auto* zoomOut=new QToolButton(this); zoomOut->setText(tr("−")); toolbar->addWidget(zoomOut);
 auto* zoomIn=new QToolButton(this); zoomIn->setText(tr("+")); toolbar->addWidget(zoomIn);
 connect(zoomOut,&QToolButton::clicked,this,[canvas]{canvas->setZoom(canvas->horizontalZoom()/1.25,canvas->verticalZoom());});
 connect(zoomIn,&QToolButton::clicked,this,[canvas]{canvas->setZoom(canvas->horizontalZoom()*1.25,canvas->verticalZoom());});
 auto* barZoom=new QComboBox(this);barZoom->setObjectName("svsBarZoom");barZoom->setToolTip(tr("Horizontal zoom: bars visible in the note area"));barZoom->addItem(tr("Custom"),0);
 for(int bars:{1,2,4,8,16}) barZoom->addItem(tr("%n bar(s)",nullptr,bars),bars);
 toolbar->addWidget(barZoom);
 connect(barZoom,qOverload<int>(&QComboBox::activated),this,[canvas,barZoom](int index){const int bars=barZoom->itemData(index).toInt();if(bars>0) canvas->setZoom(double(canvas->width()-60)/(2.*TimePos::ticksPerBar()*bars),canvas->verticalZoom());});
 connect(canvas,&SVSCanvas::viewportChanged,this,[canvas,barZoom]{const auto bars=double(canvas->width()-60)/(2.*TimePos::ticksPerBar()*canvas->horizontalZoom());int index=0;for(int i=1;i<barZoom->count();++i) if(std::abs(bars-barZoom->itemData(i).toInt())<.01) index=i;barZoom->setCurrentIndex(index);});
 toolbar->addStretch();
 auto* track=static_cast<SVSTrack*>(clip->getTrack());
 auto* sidebarScroll=new QScrollArea(this);sidebarScroll->setObjectName("svsVoicePanel");sidebarScroll->setWidgetResizable(true);sidebarScroll->setMaximumWidth(280);sidebarScroll->setMinimumWidth(220);sidebarScroll->setFrameShape(QFrame::NoFrame);body->addWidget(sidebarScroll);
 auto* sidebar=new QWidget(sidebarScroll);sidebarScroll->setWidget(sidebar);auto* sidebarLayout=new QVBoxLayout(sidebar);sidebarLayout->addWidget(new QLabel(tr("Singer"),sidebar));auto* singer=new QComboBox(sidebar);singer->setObjectName("svsSinger");sidebarLayout->addWidget(singer);
 const auto voices=svs::Registry::instance().voices();singer->addItem(tr("Select singer"),QString{});for(const auto& voice:voices) singer->addItem(voice.name,voice.pluginId+"\n"+voice.id);
 connect(singer,qOverload<int>(&QComboBox::activated),this,[track,voices](int index){if(index>0&&index<=voices.size()) track->bindVoice(voices[index-1].pluginId,voices[index-1].id);});
 auto* globalControls=new SVSGlobalControls(sidebar);sidebarLayout->addWidget(globalControls);sidebarLayout->addStretch();
 auto* side=new QDialog(this);side->setObjectName("svsEditorSettings");side->setWindowTitle(tr("SVS editor settings"));auto* controls=new QVBoxLayout(side);
 auto* settings=new QToolButton(this);settings->setText(tr("Settings"));settings->setObjectName("svsEditorSettingsButton");toolbar->addWidget(settings);connect(settings,&QToolButton::clicked,side,[side]{side->show();side->raise();});
 auto* properties=new QToolButton(this); properties->setText(tr("Properties")); properties->setCheckable(true); properties->setChecked(true); toolbar->addWidget(properties); connect(properties,&QToolButton::toggled,sidebarScroll,&QWidget::setVisible);
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
 auto refresh=[this,clip,track,canvas,lanes,parameterCanvas,tabBody,parameterTabs,selectedParameter,updateParameterDisplay,singer,globalControls,language,diagnostics,trackPanel,clipPanel,notePanel,feedbackPanel]{
  auto context=track->parameters(); for(const auto& p:track->capabilities().parameters) if(p.scope=="track"&&!context.contains(p.id)) context[p.id]=p.defaultValue;
  for(auto i=clip->parameters().begin();i!=clip->parameters().end();++i) context[i.key()]=i.value();
  singer->setCurrentIndex(std::max(0,singer->findData(track->pluginId()+"\n"+track->voiceId())));singer->setEnabled(!track->readOnly());
  globalControls->refresh(track->capabilities().parameters,track->parameters(),clip->parameters(),context,[track,clip](const svs::Parameter& p,double value){if(p.scope=="track") track->setParameter(p.id,value);else clip->setParameter(p.id,value);});globalControls->setEnabled(!clip->readOnly()&&!track->readOnly());
  trackPanel->refresh(track->capabilities().parameters,"track",{track->parameters()},context,[track](const QString& id,const QJsonValue& value){track->setParameter(id,value);});
  clipPanel->refresh(track->capabilities().parameters,"clip",{clip->parameters()},context,[clip](const QString& id,const QJsonValue& value){clip->setParameter(id,value);});
  QVector<QJsonObject> selected; QStringList selectedIds; for(const auto& note:clip->notes()) if(canvas->selectedNotes().contains(note.id)) { selected<<note.parameters; selectedIds<<note.id; }
  notePanel->refresh(track->capabilities().parameters,"note",selected,context,[clip,selectedIds](const QString& id,const QJsonValue& value){clip->setNoteParameter(selectedIds,id,value);}); notePanel->setEnabled(!selected.isEmpty());
  auto audio=clip->audio(); feedbackPanel->refresh(track->capabilities().feedbackParameters,"clip",audio?QVector<QJsonObject>{audio->feedback["parameters"].toObject()}:QVector<QJsonObject>{},context,{});
  QStringList current; for(int i=0;i<language->count();++i) current<<language->itemData(i).toString();
  if(current!=track->capabilities().languages) { language->clear(); for(const auto& value:track->capabilities().languages) { auto label=QLocale(value).nativeLanguageName(); language->addItem(label.isEmpty()?value:label,value); } }
  language->setCurrentIndex(language->findData(track->language())); diagnostics->setText(track->capabilityDiagnostics().join('\n'));
  for(auto& lane:*lanes) lane.available=false;
  QStringList available;
  auto configureLane=[&](const svs::Parameter& parameter,bool feedback) {
   if(!parameter.curve||parameter.type=="string") return;
   const auto key=(feedback?"feedback:":"input:")+parameter.id;
   if(!lanes->contains(key)) {
    auto* tab=new SVSParameterTab(tabBody);tab->setObjectName("svsParameterTab."+key);tab->setCheckable(true);tab->setAutoRaise(true);tab->setToolButtonStyle(Qt::ToolButtonTextOnly);tab->setFocusPolicy(Qt::NoFocus);
    parameterTabs->insertWidget(parameterTabs->count()-1,tab);lanes->insert(key,{tab,parameter,feedback});
    connect(tab,&QToolButton::clicked,this,[clip,key,selectedParameter,updateParameterDisplay,parameterCanvas]{
     parameterCanvas->cancelOperation();*selectedParameter=key;
     auto state=clip->editorState();auto states=state["lanes"].toObject();auto settings=states[key].toObject();settings["visible"]=true;states[key]=settings;state["lanes"]=states;state["selectedParameter"]=*selectedParameter;clip->setEditorState(state);updateParameterDisplay();parameterCanvas->setFocus();
    });
    tab->toggleVisibility=[clip,key,selectedParameter,updateParameterDisplay]{
     if(*selectedParameter==key) return;
     auto state=clip->editorState();auto states=state["lanes"].toObject();auto settings=states[key].toObject();settings["visible"]=!settings["visible"].toBool(true);states[key]=settings;state["lanes"]=states;clip->setEditorState(state);updateParameterDisplay();
    };
   }
   auto& lane=(*lanes)[key];lane.parameter=parameter;lane.feedback=feedback;lane.available=parameter.isVisible(context);
   lane.tab->setText((feedback?tr("Result: "):QString{})+parameter.name);
   const QColor declared(parameter.color);lane.tab->curveColor=declared.isValid()?declared:palette().highlight().color();
   lane.tab->setToolTip((feedback?tr("Read-only result. "):QString{})+tr("Left click: edit curve; right click: show/hide overlay.")+"\n"+parameter.disabledReason);
   if(lane.available) available.append(key);
  };
  for(const auto& parameter:track->capabilities().parameters) configureLane(parameter,false);
  for(const auto& parameter:track->capabilities().feedbackParameters) configureLane(parameter,true);
  // Select a declared curve on first opening; retain unavailable selections for voice restoration.
  if(!clip->editorState().contains("selectedParameter")&&!available.isEmpty()) { *selectedParameter=available.first();auto state=clip->editorState();state["selectedParameter"]=*selectedParameter;clip->setEditorState(state); }
  updateParameterDisplay();

 };
 connect(clip,&Clip::dataChanged,this,refresh); connect(track,&Track::dataChanged,this,refresh); connect(canvas,&SVSCanvas::selectionChanged,this,refresh); refresh();
 auto migrationState=[clip,track,canvas,strip,parameterBody,tabs,lyrics,language,import]{const bool editable=!clip->readOnly();canvas->setEnabled(editable);strip->setEnabled(editable);parameterBody->setEnabled(editable);tabs->setEnabled(editable);lyrics->setEnabled(editable);language->setEnabled(!track->readOnly());import->setEnabled(editable);};
 connect(clip,&Clip::dataChanged,this,migrationState);connect(track,&Track::dataChanged,this,migrationState);migrationState();
}
bool SVSPianoRoll::eventFilter(QObject* target,QEvent* event) {
 if(target==this&&event->type()==QEvent::Close&&m_subWindow&&m_subWindow->isDetached()) {
  // Reuse LMMS geometry/flags restoration, without hiding the returned editor.
  const bool detachable=m_subWindow->isDetachable();m_subWindow->setDetachable(false);
  m_subWindow->attach();m_subWindow->show();m_subWindow->raise();m_subWindow->setDetachable(detachable);event->ignore();return true;
 }
 return QWidget::eventFilter(target,event);
}
void SVSPianoRoll::setThemeColor(const QString& name,const QColor& value) {
 if(value.isValid()) m_colors[name]=value; else m_colors.remove(name);
 for(auto* canvas:findChildren<SVSCanvas*>()) canvas->setThemeColors(m_colors);
 for(auto* strip:findChildren<SVSResultStrip*>()) strip->setThemeColors(m_colors);
}
void SVSPianoRoll::changeEvent(QEvent* event) {
 QWidget::changeEvent(event);
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
