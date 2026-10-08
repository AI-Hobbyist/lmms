#include "SVSProjectImportDialog.h"
#include "SVSModel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <limits>

namespace lmms::gui {
SVSProjectOptionsWidget::SVSProjectOptionsWidget(const QJsonObject& defaults,const QJsonObject& schema,QWidget* parent):QWidget(parent) {
 auto* form=new QFormLayout(this);const auto properties=schema["properties"].toObject();
 for(auto i=defaults.begin();i!=defaults.end();++i) {
  const auto key=i.key();const auto value=i.value();auto descriptor=properties[key].toObject();
  if(descriptor.contains("$ref")) descriptor=schema["$defs"].toObject()[descriptor["$ref"].toString().section('/',-1)].toObject();
  auto label=properties[key].toObject()["title"].toString(key);
  QWidget* control=nullptr;
  if(descriptor["enum"].isArray()) {
   auto* combo=new QComboBox(this);for(const auto& choice:descriptor["enum"].toArray()) combo->addItem(choice.toVariant().toString(),choice.toVariant());
   combo->setCurrentIndex(std::max(0,combo->findData(value.toVariant())));control=combo;m_values[key]=[combo]{return QJsonValue::fromVariant(combo->currentData());};
  } else if(value.isBool()) {
   auto* check=new QCheckBox(this);check->setChecked(value.toBool());control=check;m_values[key]=[check]{return QJsonValue(check->isChecked());};
   if(key=="import_pitch"||key=="use_edited_pitch") {check->setChecked(true);check->setEnabled(false);label=QStringLiteral("保留原工程音高线");}
  } else if(value.isDouble()) {
   auto* spin=new QDoubleSpinBox(this);const bool integral=descriptor["type"].toString()=="integer";
   spin->setDecimals(integral?0:8);spin->setRange(descriptor["minimum"].toDouble(-std::numeric_limits<int>::max()),descriptor["maximum"].toDouble(std::numeric_limits<int>::max()));spin->setValue(value.toDouble());control=spin;m_values[key]=[spin]{return QJsonValue(spin->value());};
  } else {
   auto* edit=new QLineEdit(value.toString(),this);control=edit;m_values[key]=[edit]{return QJsonValue(edit->text());};
  }
  control->setObjectName("svsProjectOption_"+key);control->setToolTip(properties[key].toObject()["description"].toString());form->addRow(label,control);
 }
}
QJsonObject SVSProjectOptionsWidget::options() const {QJsonObject result;for(auto i=m_values.begin();i!=m_values.end();++i) result[i.key()]=i.value()();return result;}

SVSProjectImportDialog::SVSProjectImportDialog(const QJsonObject& format,QWidget* parent):QDialog(parent) {
 setObjectName("svsProjectImportDialog");setWindowTitle(QStringLiteral("导入SVS工程"));resize(540,430);
 auto* layout=new QVBoxLayout(this);auto* explanation=new QLabel(QStringLiteral("作为新工程导入。所有歌声轨将使用此次选择的声库。"),this);explanation->setWordWrap(true);layout->addWidget(explanation);
 auto* form=new QFormLayout;m_voices=new QComboBox(this);m_voices->setObjectName("svsProjectDefaultVoice");form->addRow(QStringLiteral("默认声库"),m_voices);layout->addLayout(form);
 m_status=new QLabel(this);m_status->setWordWrap(true);m_status->setObjectName("svsProjectVoiceStatus");layout->addWidget(m_status);
 auto* group=new QGroupBox(QStringLiteral("%1 输入选项").arg(format["name"].toString()),this);auto* groupLayout=new QVBoxLayout(group);auto* scroll=new QScrollArea(group);scroll->setWidgetResizable(true);
 m_options=new SVSProjectOptionsWidget(format["inputDefaults"].toObject(),format["inputSchema"].toObject(),scroll);scroll->setWidget(m_options);groupLayout->addWidget(scroll);layout->addWidget(group);
 if(format["id"].toString()=="svp") {if(auto* pitch=m_options->findChild<QComboBox*>("svsProjectOption_pitch")) {pitch->setCurrentIndex(pitch->findData(QStringLiteral("full")));pitch->setEnabled(false);pitch->setToolTip(QStringLiteral("完整保留原工程滑音和可解析颤音。"));}}
 m_buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("导入"));m_buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));layout->addWidget(m_buttons);
 connect(m_buttons,&QDialogButtonBox::accepted,this,&SVSProjectImportDialog::accept);connect(m_buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
 auto& registry=svs::Registry::instance();connect(&registry,&svs::Registry::catalogChanged,this,[this]{refreshVoices();});connect(&registry,&svs::Registry::catalogScanStarted,this,[this]{refreshVoices();});connect(&registry,&svs::Registry::catalogScanFinished,this,[this]{refreshVoices();});refreshVoices();
}
void SVSProjectImportDialog::refreshVoices() {
 const auto identity=m_voices->currentData();m_voices->clear();auto& registry=svs::Registry::instance();QMap<QString,QString> engineNames;
 for(const auto& engine:registry.engines()) engineNames[engine.id]=engine.name;
 for(const auto& voice:registry.voices()) {
  m_voices->addItem(QStringLiteral("%1 — %2").arg(voice.name,engineNames.value(voice.pluginId,voice.pluginId)),QVariantList{voice.pluginId,voice.id});
 }
 const int selected=m_voices->findData(identity);if(selected>=0) m_voices->setCurrentIndex(selected);
 m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_voices->count()>0);
 m_status->setText(registry.scanning()?QStringLiteral("正在扫描声库，列表将自动更新…"):m_voices->count()?QStringLiteral("默认声库仅用于此次导入。"):QStringLiteral("没有可用声库。请先在 SVS 设置中配置声库。"));
}
void SVSProjectImportDialog::accept() {
 const auto identity=m_voices->currentData().toList();if(identity.size()!=2) return;
 for(const auto& voice:svs::Registry::instance().voices()) if(voice.pluginId==identity[0].toString()&&voice.id==identity[1].toString()) {
  m_selected={voice.pluginId,voice.id,voice.name,voice.language};QDialog::accept();return;
 }
 refreshVoices();m_status->setText(QStringLiteral("所选声库已不可用，请重新选择。"));
}
QJsonObject SVSProjectImportDialog::options() const {return m_options->options();}

SVSProjectExportDialog::SVSProjectExportDialog(const QJsonObject& format,const QJsonObject& project,QWidget* parent):QDialog(parent),m_policy(format["exportPolicy"].toObject()) {
 setObjectName("svsProjectExportDialog");setWindowTitle(QStringLiteral("导出SVS工程"));resize(560,450);
 auto* layout=new QVBoxLayout(this);auto* explanation=new QLabel(QStringLiteral("导出点击时的工程快照。无需声库或合成音频；受限格式必须选择轨道，省略内容将在下一步列出。"),this);explanation->setWordWrap(true);layout->addWidget(explanation);
 auto* form=new QFormLayout;m_singing=new QComboBox(this);m_audio=new QComboBox(this);m_singing->setObjectName("svsProjectExportSingingTrack");m_audio->setObjectName("svsProjectExportAudioTrack");
 m_singing->addItem(QStringLiteral("请选择歌声轨…"),-1);m_audio->addItem(QStringLiteral("请选择伴奏轨…"),-1);
 const auto tracks=project["track_list"].toArray();for(int index=0;index<tracks.size();++index) {const auto track=tracks[index].toObject();(track["type_"].toString()=="Singing"?m_singing:m_audio)->addItem(QStringLiteral("%1 · %2").arg(index+1).arg(track["title"].toString()),index);}
 if(m_singing->count()==2) m_singing->setCurrentIndex(1);if(m_audio->count()==2) m_audio->setCurrentIndex(1);
 form->addRow(QStringLiteral("受限格式的歌声轨"),m_singing);form->addRow(QStringLiteral("受限格式的伴奏轨"),m_audio);layout->addLayout(form);
 auto* group=new QGroupBox(QStringLiteral("%1 [%2] 输出选项").arg(format["name"].toString(),format["id"].toString()),this);auto* groupLayout=new QVBoxLayout(group);auto* scroll=new QScrollArea(group);scroll->setWidgetResizable(true);
 m_options=new SVSProjectOptionsWidget(format["outputDefaults"].toObject(),format["outputSchema"].toObject(),scroll);scroll->setWidget(m_options);groupLayout->addWidget(scroll);layout->addWidget(group);
 // The named selector owns original indices; preflight remaps them after projection.
 if(auto* index=m_options->findChild<QWidget*>("svsProjectOption_track_index")) {index->setEnabled(false);index->setToolTip(QStringLiteral("由上方轨道选择自动设置。"));}
 m_buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("检查并导出"));m_buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));layout->addWidget(m_buttons);
 connect(m_buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);connect(m_buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
 connect(m_singing,&QComboBox::currentIndexChanged,this,[this]{refreshSelection();});connect(m_audio,&QComboBox::currentIndexChanged,this,[this]{refreshSelection();});
 if(auto* version=m_options->findChild<QDoubleSpinBox*>("svsProjectOption_version")) connect(version,&QDoubleSpinBox::valueChanged,this,[this]{refreshSelection();});
 refreshSelection();
}
void SVSProjectExportDialog::refreshSelection() {
 const bool singing=m_policy["singing"].toInt()==1||(m_policy.contains("legacySinging")&&options()["version"].toDouble(2)<2);
 const bool audio=m_policy["audio"].toInt()==1;m_singing->setEnabled(singing);m_audio->setEnabled(audio);
 m_buttons->button(QDialogButtonBox::Ok)->setEnabled((!singing||m_singing->count()==1||m_singing->currentData().toInt()>=0)&&(!audio||m_audio->count()==1||m_audio->currentData().toInt()>=0));
}
QJsonObject SVSProjectExportDialog::options() const {auto result=m_options->options();if(result.contains("track_index")) result["track_index"]=-1;return result;}
QJsonObject SVSProjectExportDialog::selection() const {QJsonObject result;if(m_singing->isEnabled()&&m_singing->currentData().toInt()>=0) result["singingTrack"]=m_singing->currentData().toInt();if(m_audio->isEnabled()&&m_audio->currentData().toInt()>=0) result["audioTrack"]=m_audio->currentData().toInt();return result;}
}
