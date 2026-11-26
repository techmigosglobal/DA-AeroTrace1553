#include "main_window.hpp"
#include "services/manual_mapping.hpp"
#include "services/operator_style.hpp"
#include "services/profile_io.hpp"
#include "ui_manual_mappings.h"

namespace {
QString draftPath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/manual-mapping-drafts.json";
}

QComboBox *wordCountCombo(int count) {
  auto result = new QComboBox;
  for (int n = 1; n <= 32; ++n) result->addItem(QString("%1 word%2").arg(n).arg(n == 1 ? "" : "s"), n);
  result->setCurrentIndex(qBound(1, count, 32) - 1);
  return result;
}

QLabel *fieldCaption(const QString &text, QWidget *parent) {
  auto label = new QLabel(text, parent);
  label->setStyleSheet("font-size:11px;font-weight:600;color:#4a5d52;margin-bottom:1px;");
  return label;
}

class FieldCard final : public QFrame {
  QJsonObject data;
  bool expanded{true};
  QToolButton *toggleButton{};
  QLabel *summaryLabel{};
  QLabel *badgeLabel{};
  QWidget *bodyWidget{};
  QLabel *checkDetail{};
  QLineEdit *nameEdit{};
  QComboBox *wordCombo{};
  QComboBox *typeCombo{};
  QComboBox *widthCombo{};
  QComboBox *lsbCombo{};
  QComboBox *orderCombo{};
  QLineEdit *unitEdit{};
  QComboBox *modeCombo{};
  QLineEdit *scaleEdit{};
  QLineEdit *offsetEdit{};
  QLineEdit *formulaEdit{};
  QWidget *linearBox{};
  QWidget *formulaBox{};
  QCheckBox *overlapCheck{};
  QLineEdit *rawEdit{};
  QLineEdit *expectedEdit{};
  QLineEdit *toleranceEdit{};
  std::function<void()> onChanged;
  std::function<void()> onDuplicate;
  std::function<void()> onDelete;

  static QString shortSummary(const QJsonObject &f) {
    const auto name = f["name"].toString().trimmed();
    const int word = f["word"].toInt(0);
    const auto type = f["type"].toString("unsigned");
    const int width = f["width"].toInt(16);
    const int lsb = f["lsb"].toInt(0);
    const int span = (lsb + width + 15) / 16;
    const auto wordText = span > 1 ? QString("Word %1–%2").arg(word).arg(word + span - 1) : QString("Word %1").arg(word);
    const auto unit = f["unit"].toString().trimmed();
    const auto bitText = (type == "unsigned" || type == "signed" || type == "bcd")
      ? QString(" · %1b @ b%2").arg(width).arg(lsb) : QString();
    return QString("%1   ·   %2   ·   %3%4%5")
      .arg(name.isEmpty() ? QString("(Unnamed field)") : name,
           wordText, type, bitText,
           unit.isEmpty() ? QString() : QString("   ·   %1").arg(unit));
  }

  void updateModeVisibility() {
    const int idx = typeCombo->currentIndex();
    const bool variableBits = idx < 2 || idx == 4; // unsigned, signed, bcd
    widthCombo->setEnabled(variableBits);
    lsbCombo->setEnabled(variableBits);
    if (idx == 2) {
      const QSignalBlocker b1(widthCombo), b2(lsbCombo);
      widthCombo->setCurrentIndex(widthCombo->findData(32));
      lsbCombo->setCurrentIndex(0);
    } else if (idx == 3) {
      const QSignalBlocker b1(widthCombo), b2(lsbCombo);
      if (widthCombo->findData(64) < 0) widthCombo->addItem("64b", 64);
      widthCombo->setCurrentIndex(widthCombo->findData(64));
      lsbCombo->setCurrentIndex(0);
    } else if (widthCombo->currentData().toInt() > 32) {
      const QSignalBlocker b1(widthCombo);
      widthCombo->setCurrentIndex(widthCombo->findData(16));
    }
    const int bits = widthCombo->currentData().toInt();
    const int span = (lsbCombo->currentData().toInt() + bits + 15) / 16;
    orderCombo->setEnabled(span > 1);
    const bool custom = modeCombo->currentIndex() == 1;
    linearBox->setVisible(!custom);
    formulaBox->setVisible(custom);
  }

  void emitChange() {
    data = collect();
    summaryLabel->setText(shortSummary(data));
    if (onChanged) onChanged();
  }

public:
  FieldCard(const QJsonObject &field, int wordCount, bool startExpanded,
            std::function<void()> changed, std::function<void()> duplicate,
            std::function<void()> remove, QWidget *parent = nullptr)
      : QFrame(parent), data(field), expanded(startExpanded),
        onChanged(std::move(changed)), onDuplicate(std::move(duplicate)), onDelete(std::move(remove)) {
    setObjectName("manualFieldCard");
    setFrameShape(QFrame::StyledPanel);
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto headerBar = new QFrame(this);
    headerBar->setObjectName("manualFieldHeader");
    auto header = new QHBoxLayout(headerBar);
    header->setContentsMargins(10, 6, 10, 6);
    header->setSpacing(8);
    toggleButton = new QToolButton(headerBar);
    toggleButton->setAutoRaise(true);
    toggleButton->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    toggleButton->setToolTip("Expand or collapse this field");
    toggleButton->setFixedSize(24, 24);
    summaryLabel = new QLabel(shortSummary(data), headerBar);
    summaryLabel->setStyleSheet("font-weight:600;font-size:13px;color:#142d3a;");
    summaryLabel->setWordWrap(true);
    badgeLabel = new QLabel("Ready", headerBar);
    badgeLabel->setStyleSheet("background:#e2f3e8;color:#166534;border-radius:10px;padding:2px 10px;font-size:12px;font-weight:600;");
    auto dupBtn = new QPushButton("Duplicate", headerBar);
    dupBtn->setProperty("compactAction", true);
    dupBtn->setToolTip("Duplicate this field onto the next word");
    desktop::actionIcon(dupBtn, "copy", false, 14);
    auto delBtn = new QPushButton("Remove", headerBar);
    delBtn->setProperty("compactAction", true);
    desktop::actionIcon(delBtn, "cancel", false, 14);
    header->addWidget(toggleButton);
    header->addWidget(summaryLabel, 1);
    header->addWidget(badgeLabel);
    header->addWidget(dupBtn);
    header->addWidget(delBtn);
    root->addWidget(headerBar);

    bodyWidget = new QWidget(this);
    bodyWidget->setVisible(expanded);
    auto bodyLayout = new QVBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(12, 10, 12, 10);
    bodyLayout->setSpacing(8);

    // Row 1: Signal name (wide), Word (0-31), Type, Unit
    auto row1 = new QGridLayout;
    row1->setHorizontalSpacing(8);
    row1->setVerticalSpacing(2);
    nameEdit = new QLineEdit(data["name"].toString(), bodyWidget);
    nameEdit->setObjectName("fieldNameEdit");
    nameEdit->setPlaceholderText("Signal name (e.g. Pitch_Rate)");
    wordCombo = new QComboBox(bodyWidget);
    wordCombo->setObjectName("fieldWordCombo");
    setWordCount(wordCount);
    typeCombo = new QComboBox(bodyWidget);
    typeCombo->setObjectName("fieldTypeCombo");
    typeCombo->addItem("Unsigned", "unsigned");
    typeCombo->addItem("Signed", "signed");
    typeCombo->addItem("Float32", "float32");
    typeCombo->addItem("Float64", "float64");
    typeCombo->addItem("BCD", "bcd");
    typeCombo->setCurrentIndex(qMax(0, typeCombo->findData(data["type"].toString("unsigned"))));
    unitEdit = new QLineEdit(data["unit"].toString(), bodyWidget);
    unitEdit->setObjectName("fieldUnitEdit");
    unitEdit->setPlaceholderText("e.g. Counts");

    row1->addWidget(fieldCaption("Signal name", bodyWidget), 0, 0);
    row1->addWidget(fieldCaption("Word (0–31)", bodyWidget), 0, 1);
    row1->addWidget(fieldCaption("Type", bodyWidget), 0, 2);
    row1->addWidget(fieldCaption("Unit", bodyWidget), 0, 3);
    row1->addWidget(nameEdit, 1, 0);
    row1->addWidget(wordCombo, 1, 1);
    row1->addWidget(typeCombo, 1, 2);
    row1->addWidget(unitEdit, 1, 3);
    row1->setColumnStretch(0, 3);
    row1->setColumnStretch(1, 1);
    row1->setColumnStretch(2, 1);
    row1->setColumnStretch(3, 1);
    bodyLayout->addLayout(row1);

    // Row 2: Scale, Offset, Bit length, Bit offset (b0), Byte order
    widthCombo = new QComboBox(bodyWidget);
    widthCombo->setObjectName("fieldWidthCombo");
    for (int b = 1; b <= 32; ++b) widthCombo->addItem(QString("%1 bits").arg(b), b);
    const int initWidth = qBound(1, data["width"].toInt(16), 64);
    if (initWidth > 32 && widthCombo->findData(initWidth) < 0) widthCombo->addItem(QString("%1 bits").arg(initWidth), initWidth);
    widthCombo->setCurrentIndex(qMax(0, widthCombo->findData(initWidth)));

    lsbCombo = new QComboBox(bodyWidget);
    lsbCombo->setObjectName("fieldLsbCombo");
    for (int b = 0; b < 16; ++b) lsbCombo->addItem(QString("b%1").arg(b), b);
    lsbCombo->setCurrentIndex(qBound(0, data["lsb"].toInt(0), 15));

    orderCombo = new QComboBox(bodyWidget);
    orderCombo->setObjectName("fieldOrderCombo");
    orderCombo->addItems({"LSW first", "MSW first"});
    orderCombo->setToolTip("Multi-word order: LSW first = low 16 bits in first word; MSW first = high 16 bits first");
    orderCombo->setCurrentIndex(data["order"].toString() == "MSW first" ? 1 : 0);

    scaleEdit = new QLineEdit(data["scale"].toString(), bodyWidget);
    scaleEdit->setObjectName("fieldScaleEdit");
    scaleEdit->setPlaceholderText("1.0");
    offsetEdit = new QLineEdit(data["offset"].toString(), bodyWidget);
    offsetEdit->setObjectName("fieldOffsetEdit");
    offsetEdit->setPlaceholderText("0.0");

    auto row2 = new QGridLayout;
    row2->setHorizontalSpacing(8);
    row2->setVerticalSpacing(2);
    row2->addWidget(fieldCaption("Scale", bodyWidget), 0, 0);
    row2->addWidget(fieldCaption("Offset", bodyWidget), 0, 1);
    row2->addWidget(fieldCaption("Bit length", bodyWidget), 0, 2);
    row2->addWidget(fieldCaption("Bit offset (b0)", bodyWidget), 0, 3);
    row2->addWidget(fieldCaption("Byte order", bodyWidget), 0, 4);
    row2->addWidget(scaleEdit, 1, 0);
    row2->addWidget(offsetEdit, 1, 1);
    row2->addWidget(widthCombo, 1, 2);
    row2->addWidget(lsbCombo, 1, 3);
    row2->addWidget(orderCombo, 1, 4);
    for (int c = 0; c < 5; ++c) row2->setColumnStretch(c, 1);
    bodyLayout->addLayout(row2);

    // Row 3: Conversion mode / Custom formula + Overlap opt-in
    overlapCheck = new QCheckBox("Allow bit overlap", bodyWidget);
    overlapCheck->setObjectName("fieldOverlapCheck");
    overlapCheck->setChecked(data["allow_overlap"].toBool(false));
    overlapCheck->setToolTip("Enable when mapping both a whole word and discrete bit flags at the same word");

    auto row3Conv = new QGridLayout;
    row3Conv->setHorizontalSpacing(8);
    row3Conv->setVerticalSpacing(2);
    modeCombo = new QComboBox(bodyWidget);
    modeCombo->setObjectName("fieldModeCombo");
    modeCombo->addItems({"Linear scale", "Custom formula"});
    modeCombo->setCurrentIndex(data["formula"].toString().trimmed().isEmpty() ? 0 : 1);

    linearBox = new QWidget(bodyWidget);
    auto linearLayout = new QHBoxLayout(linearBox);
    linearLayout->setContentsMargins(0, 0, 0, 0);
    linearLayout->addWidget(overlapCheck);

    formulaBox = new QWidget(bodyWidget);
    auto formulaLayout = new QGridLayout(formulaBox);
    formulaLayout->setContentsMargins(0, 0, 0, 0);
    formulaLayout->setVerticalSpacing(2);
    formulaEdit = new QLineEdit(data["formula"].toString(), formulaBox);
    formulaEdit->setObjectName("fieldFormulaEdit");
    formulaEdit->setPlaceholderText("e.g. x * 0.008239997 or pow(x,2)/100");
    formulaLayout->addWidget(fieldCaption("Formula (x = decoded raw value)", formulaBox), 0, 0);
    formulaLayout->addWidget(formulaEdit, 1, 0);

    row3Conv->addWidget(fieldCaption("Conversion", bodyWidget), 0, 0);
    row3Conv->addWidget(modeCombo, 1, 0);
    row3Conv->addWidget(linearBox, 0, 1, 2, 1);
    row3Conv->addWidget(formulaBox, 0, 1, 2, 1);
    row3Conv->setColumnStretch(0, 1);
    row3Conv->setColumnStretch(1, 2);
    bodyLayout->addLayout(row3Conv);

    // Row 3: Optional hex check strip
    auto checkBox = new QFrame(bodyWidget);
    checkBox->setStyleSheet("QFrame{background:#f7faf8;border:1px solid #e1ebe4;border-radius:6px;}");
    auto checkLayout = new QVBoxLayout(checkBox);
    checkLayout->setContentsMargins(8, 6, 8, 6);
    checkLayout->setSpacing(4);
    auto row3 = new QGridLayout;
    row3->setHorizontalSpacing(8);
    row3->setVerticalSpacing(2);
    const auto samples = data["examples"].toArray();
    const auto ex0 = samples.isEmpty() ? data : samples.at(0).toObject();
    rawEdit = new QLineEdit(ex0["raw"].toString(), checkBox);
    rawEdit->setObjectName("fieldRawEdit");
    rawEdit->setPlaceholderText("Optional: 3F80 0000 or 3F800000");
    expectedEdit = new QLineEdit(ex0["expected"].toVariant().toString(), checkBox);
    expectedEdit->setObjectName("fieldExpectedEdit");
    expectedEdit->setPlaceholderText("Optional expected value");
    toleranceEdit = new QLineEdit(data["tolerance"].toString(), checkBox);
    toleranceEdit->setObjectName("fieldToleranceEdit");
    toleranceEdit->setPlaceholderText("0");

    row3->addWidget(fieldCaption("Test hex (optional)", checkBox), 0, 0);
    row3->addWidget(fieldCaption("Expected value (optional)", checkBox), 0, 1);
    row3->addWidget(fieldCaption("Tolerance", checkBox), 0, 2);
    row3->addWidget(rawEdit, 1, 0);
    row3->addWidget(expectedEdit, 1, 1);
    row3->addWidget(toleranceEdit, 1, 2);
    row3->setColumnStretch(0, 2);
    row3->setColumnStretch(1, 2);
    row3->setColumnStretch(2, 1);
    checkLayout->addLayout(row3);

    checkDetail = new QLabel(checkBox);
    checkDetail->setWordWrap(true);
    checkDetail->setStyleSheet("background:transparent;border:0;color:#4c5a4a;font-size:11px;");
    checkLayout->addWidget(checkDetail);
    bodyLayout->addWidget(checkBox);

    root->addWidget(bodyWidget);
    updateModeVisibility();

    connect(toggleButton, &QToolButton::clicked, this, [this] { setExpanded(!expanded); });
    connect(dupBtn, &QPushButton::clicked, this, [this] { if (onDuplicate) onDuplicate(); });
    connect(delBtn, &QPushButton::clicked, this, [this] { if (onDelete) onDelete(); });
    for (auto line : {nameEdit, unitEdit, scaleEdit, offsetEdit, formulaEdit, rawEdit, expectedEdit, toleranceEdit})
      connect(line, &QLineEdit::textChanged, this, [this] { emitChange(); });
    for (auto combo : {wordCombo, typeCombo, widthCombo, lsbCombo, orderCombo, modeCombo})
      connect(combo, &QComboBox::currentIndexChanged, this, [this] {
        updateModeVisibility();
        emitChange();
      });
    connect(overlapCheck, &QCheckBox::toggled, this, [this] { emitChange(); });
  }

  void setExpanded(bool on) {
    expanded = on;
    bodyWidget->setVisible(on);
    toggleButton->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
  }
  bool isExpanded() const { return expanded; }
  void focusName() { nameEdit->setFocus(); nameEdit->selectAll(); }

  void setWordCount(int wordCount) {
    const QSignalBlocker block(wordCombo);
    const int current = wordCombo->count() ? wordCombo->currentData().toInt() : data["word"].toInt(0);
    wordCombo->clear();
    for (int w = 0; w < qMax(1, wordCount); ++w)
      wordCombo->addItem(QString("Word %1").arg(w), w);
    wordCombo->setCurrentIndex(qBound(0, current, qMax(0, wordCount - 1)));
  }

  void setStatus(const QString &status, bool ok) {
    badgeLabel->setText(ok ? (status.startsWith("Pass") ? "Pass" : "Ready") : "Needs fix");
    badgeLabel->setStyleSheet(ok
      ? "color:#145331;background:#e2f2e8;font-size:11px;padding:3px 9px;border-radius:10px;font-weight:600;"
      : "color:#8a2117;background:#fce8e6;font-size:11px;padding:3px 9px;border-radius:10px;font-weight:600;");
    checkDetail->setText(status);
    checkDetail->setStyleSheet(ok ? "background:transparent;border:0;color:#29573b;font-size:11px;" : "background:transparent;border:0;color:#9f2c20;font-size:11px;");
  }

  QJsonObject collect() const {
    auto f = data;
    const auto type = typeCombo->currentData().toString();
    const bool isFloat = type == "float32" || type == "float64";
    const int bits = type == "float32" ? 32 : type == "float64" ? 64 : qMin(32, widthCombo->currentData().toInt());
    const int lsb = isFloat ? 0 : lsbCombo->currentData().toInt();
    const int span = (lsb + bits + 15) / 16;
    const bool custom = modeCombo->currentIndex() == 1;
    f["id"] = f["id"].toString().isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : f["id"].toString();
    f["name"] = nameEdit->text().trimmed();
    f["word"] = wordCombo->currentData().toInt();
    f["lsb"] = lsb;
    f["width"] = bits;
    f["type"] = type;
    f["order"] = span > 1 ? orderCombo->currentText() : QString();
    f["advanced"] = isFloat || lsb != 0 || bits != 16 || type != "unsigned";
    f["unit"] = unitEdit->text().trimmed();
    f["custom"] = custom;
    f["scale"] = custom ? QString() : scaleEdit->text().trimmed();
    f["offset"] = custom ? QString() : offsetEdit->text().trimmed();
    f["formula"] = custom ? formulaEdit->text().trimmed() : QString();
    f["allow_overlap"] = overlapCheck->isChecked();
    f["raw"] = rawEdit->text().trimmed();
    f["expected"] = expectedEdit->text().trimmed();
    f["tolerance"] = toleranceEdit->text().trimmed();
    auto examples = data["examples"].toArray();
    if (!f["raw"].toString().isEmpty() || !f["expected"].toString().isEmpty()) {
      QJsonObject first{{"raw", f["raw"]}, {"expected", f["expected"]}};
      if (examples.isEmpty()) examples.append(first);
      else examples.replace(0, first);
    } else if (!examples.isEmpty()) {
      examples.removeAt(0);
    }
    f["examples"] = examples;
    return f;
  }
};

class CommandEditor final : public QWidget {
  QJsonObject definition;
  QStringList subsystems;
  QLabel *routeLabel{};
  QFrame *routeCard{};
  QWidget *routeBody{};
  QToolButton *routeToggle{};
  QComboBox *subsystemCombo{};
  QComboBox *rtCombo{};
  QComboBox *saCombo{};
  QComboBox *directionCombo{};
  QComboBox *countCombo{};
  QComboBox *channelCombo{};
  QComboBox *busCombo{};
  QComboBox *matchWordCombo{};
  QLineEdit *matchValueEdit{};
  QPushButton *addFieldBtn{};
  QPushButton *bulkAddBtn{};
  QPushButton *expandAllBtn{};
  QPushButton *collapseAllBtn{};
  QScrollArea *cardsScroll{};
  QWidget *cardsContainer{};
  QVBoxLayout *cardsLayout{};
  QList<FieldCard *> cards;
  std::function<void()> onModified;
  std::function<void()> onSubsystemCommitted;
  bool loading{};

  void syncRouteFromUi() {
    if (loading || definition.isEmpty()) return;
    definition["word_base"] = 0;
    definition["subsystem"] = subsystemCombo->currentText().trimmed();
    definition["rt"] = rtCombo->currentData().toInt();
    definition["sa"] = saCombo->currentData().toInt();
    definition["tx"] = directionCombo->currentIndex() == 1;
    const int wc = countCombo->currentData().toInt();
    definition["count"] = wc;
    definition["channel"] = channelCombo->currentIndex();
    definition["bus"] = busCombo->currentIndex() - 1;
    const int mw = matchWordCombo->currentData().toInt();
    definition["match_word"] = mw;
    definition["match_value"] = mw >= 0 ? matchValueEdit->text().trimmed() : QString();
    matchValueEdit->setEnabled(mw >= 0);
    for (auto card : cards) card->setWordCount(wc);
    updateTagWordChoices(wc, mw);
    routeLabel->setText(desktop::manualRouteLabel(definition) + " · Words 0–" + QString::number(wc - 1) + " (0-based)");
    if (onModified) onModified();
  }

  void syncFieldsFromCards() {
    if (loading || definition.isEmpty()) return;
    QJsonArray array;
    for (auto card : cards) array.append(card->collect());
    definition["fields"] = array;
    definition["word_base"] = 0;
    validateExamples();
    if (onModified) onModified();
  }

  void updateTagWordChoices(int wc, int currentTag) {
    const QSignalBlocker block(matchWordCombo);
    matchWordCombo->clear();
    matchWordCombo->addItem("No tag filter", -1);
    for (int w = 0; w < wc; ++w)
      matchWordCombo->addItem(QString("Word %1").arg(w), w);
    const int idx = matchWordCombo->findData(currentTag);
    matchWordCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    matchValueEdit->setEnabled(matchWordCombo->currentData().toInt() >= 0);
  }

  void rebuildCards(int expandIndex = -1) {
    qDeleteAll(cards);
    cards.clear();
    const auto rows = definition["fields"].toArray();
    const int wc = qBound(1, definition["count"].toInt(32), 32);
    for (int i = 0; i < rows.size(); ++i) {
      const bool startOpen = expandIndex == i || (expandIndex == -2) || rows.size() <= 4;
      auto card = new FieldCard(
        rows[i].toObject(), wc, startOpen,
        [this] { syncFieldsFromCards(); },
        [this, i] { duplicateFieldAt(i); },
        [this, i] { removeFieldAt(i); },
        cardsContainer);
      cardsLayout->insertWidget(cardsLayout->count() - 1, card);
      card->show();
      cards.append(card);
    }
    validateExamples();
  }

  void duplicateFieldAt(int index) {
    auto entries = definition["fields"].toArray();
    if (index < 0 || index >= entries.size() || entries.size() >= 512) return;
    const int wc = qBound(1, definition["count"].toInt(32), 32);
    auto copy = entries[index].toObject();
    const int span = (copy["lsb"].toInt() + copy["width"].toInt(16) + 15) / 16;
    const int nextWord = qMin(wc - 1, copy["word"].toInt() + span);
    copy["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy["word"] = nextWord;
    copy["name"] = QString("Signal_%1").arg(nextWord);
    copy.remove("raw");
    copy.remove("expected");
    copy.remove("examples");
    entries.insert(index + 1, copy);
    definition["fields"] = entries;
    rebuildCards(index + 1);
    if (index + 1 < cards.size()) {
      cards[index + 1]->setExpanded(true);
      cardsScroll->ensureWidgetVisible(cards[index + 1]);
      cards[index + 1]->focusName();
    }
    if (onModified) onModified();
  }

  void removeFieldAt(int index) {
    auto entries = definition["fields"].toArray();
    if (index < 0 || index >= entries.size()) return;
    const auto field = entries[index].toObject()["name"].toString();
    if (QMessageBox::question(this, "Remove field",
          QString("Remove %1 from this command?").arg(field.isEmpty() ? QString("this field") : field),
          QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
      return;
    entries.removeAt(index);
    definition["fields"] = entries;
    rebuildCards();
    if (onModified) onModified();
  }

public:
  explicit CommandEditor(QWidget *parent = nullptr) : QWidget(parent) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    routeCard = new QFrame(this);
    routeCard->setObjectName("manualRouteCard");
    auto routeBox = new QVBoxLayout(routeCard);
    routeBox->setContentsMargins(10, 8, 10, 8);
    routeBox->setSpacing(6);

    auto routeHeader = new QHBoxLayout;
    routeHeader->setSpacing(8);
    routeToggle = new QToolButton(routeCard);
    routeToggle->setAutoRaise(true);
    routeToggle->setArrowType(Qt::DownArrow);
    routeToggle->setToolTip("Show or hide command route settings");
    routeToggle->setFixedSize(22, 22);
    routeLabel = new QLabel("Select or add a command to map fields (Word 0 to Word 31, 0-based).", routeCard);
    routeLabel->setWordWrap(true);
    routeLabel->setObjectName("manualCommandWord");
    routeLabel->setStyleSheet("font-weight:700;font-size:13px;color:#145331;");
    routeHeader->addWidget(routeToggle);
    routeHeader->addWidget(routeLabel, 1);
    routeBox->addLayout(routeHeader);

    routeBody = new QWidget(routeCard);
    auto grid = new QGridLayout(routeBody);
    grid->setContentsMargins(0, 2, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(2);
    subsystemCombo = new QComboBox(routeBody);
    subsystemCombo->setEditable(true);
    subsystemCombo->setInsertPolicy(QComboBox::NoInsert);
    subsystemCombo->setObjectName("manualSubsystem");
    rtCombo = new QComboBox(routeBody);
    rtCombo->setObjectName("manualRt");
    for (int r = 0; r <= 30; ++r) rtCombo->addItem(QString("RT %1").arg(r), r);
    saCombo = new QComboBox(routeBody);
    saCombo->setObjectName("manualSa");
    for (int s = 1; s <= 30; ++s) saCombo->addItem(QString("SA %1").arg(s), s);
    directionCombo = new QComboBox(routeBody);
    directionCombo->setObjectName("manualDirection");
    directionCombo->addItems({"BC → RT", "RT → BC"});
    countCombo = wordCountCombo(32);
    countCombo->setObjectName("manualCount");

    channelCombo = new QComboBox(routeBody);
    channelCombo->setObjectName("manualChannel");
    channelCombo->addItems({"Any channel", "CH1 · External", "CH2 · Internal"});
    busCombo = new QComboBox(routeBody);
    busCombo->setObjectName("manualBus");
    busCombo->addItems({"Either bus", "Bus A", "Bus B"});
    matchWordCombo = new QComboBox(routeBody);
    matchWordCombo->setObjectName("manualMatchWord");
    matchValueEdit = new QLineEdit(routeBody);
    matchValueEdit->setObjectName("manualMatchValue");
    matchValueEdit->setPlaceholderText("e.g. 9999");

    grid->addWidget(fieldCaption("Subsystem", routeBody), 0, 0);
    grid->addWidget(fieldCaption("RT", routeBody), 0, 1);
    grid->addWidget(fieldCaption("SA", routeBody), 0, 2);
    grid->addWidget(fieldCaption("Direction", routeBody), 0, 3);
    grid->addWidget(fieldCaption("Word count", routeBody), 0, 4);
    grid->addWidget(subsystemCombo, 1, 0);
    grid->addWidget(rtCombo, 1, 1);
    grid->addWidget(saCombo, 1, 2);
    grid->addWidget(directionCombo, 1, 3);
    grid->addWidget(countCombo, 1, 4);

    grid->addWidget(fieldCaption("Channel", routeBody), 2, 0);
    grid->addWidget(fieldCaption("Bus", routeBody), 2, 1);
    grid->addWidget(fieldCaption("Frame tag word (optional mux)", routeBody), 2, 2, 1, 2);
    grid->addWidget(fieldCaption("Frame tag hex", routeBody), 2, 4);
    grid->addWidget(channelCombo, 3, 0);
    grid->addWidget(busCombo, 3, 1);
    grid->addWidget(matchWordCombo, 3, 2, 1, 2);
    grid->addWidget(matchValueEdit, 3, 4);
    grid->setColumnStretch(0, 2);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);
    grid->setColumnStretch(4, 1);
    routeBox->addWidget(routeBody);
    layout->addWidget(routeCard);

    connect(routeToggle, &QToolButton::clicked, this, [this] {
      const bool next = !routeBody->isVisible();
      routeBody->setVisible(next);
      routeToggle->setArrowType(next ? Qt::DownArrow : Qt::RightArrow);
    });

    auto actions = new QHBoxLayout;
    actions->setSpacing(6);
    auto sectionTitle = new QLabel("Signal fields (Word 0 to Word 31)", this);
    sectionTitle->setStyleSheet("font-weight:600;font-size:13px;color:#20394c;");
    addFieldBtn = new QPushButton("Add field", this);
    addFieldBtn->setObjectName("addManualField");
    addFieldBtn->setProperty("compactAction", true);
    bulkAddBtn = new QPushButton("Add multiple words…", this);
    bulkAddBtn->setObjectName("addMultipleManualFields");
    bulkAddBtn->setProperty("compactAction", true);
    expandAllBtn = new QPushButton("Expand all", this);
    expandAllBtn->setObjectName("expandAllManualFields");
    expandAllBtn->setProperty("compactAction", true);
    collapseAllBtn = new QPushButton("Collapse all", this);
    collapseAllBtn->setObjectName("collapseAllManualFields");
    collapseAllBtn->setProperty("compactAction", true);
    desktop::actionIcon(addFieldBtn, "add-file", false, 14);
    desktop::actionIcon(bulkAddBtn, "add-file", false, 14);
    desktop::actionIcon(expandAllBtn, "expand", false, 14);
    desktop::actionIcon(collapseAllBtn, "collapse", false, 14);
    actions->addWidget(sectionTitle);
    actions->addStretch();
    actions->addWidget(addFieldBtn);
    actions->addWidget(bulkAddBtn);
    actions->addWidget(expandAllBtn);
    actions->addWidget(collapseAllBtn);
    layout->addLayout(actions);

    cardsScroll = new QScrollArea(this);
    cardsScroll->setWidgetResizable(true);
    cardsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cardsScroll->setFrameShape(QFrame::NoFrame);
    cardsContainer = new QWidget(cardsScroll);
    cardsContainer->setObjectName("manualFieldCards");
    cardsLayout = new QVBoxLayout(cardsContainer);
    cardsLayout->setContentsMargins(0, 0, 4, 0);
    cardsLayout->setSpacing(8);
    cardsLayout->addStretch();
    cardsScroll->setWidget(cardsContainer);
    layout->addWidget(cardsScroll, 1);

    connect(subsystemCombo, &QComboBox::currentTextChanged, this, [this] { syncRouteFromUi(); });
    if (auto line = subsystemCombo->lineEdit()) {
      connect(line, &QLineEdit::editingFinished, this, [this] {
        syncRouteFromUi();
        if (onSubsystemCommitted) onSubsystemCommitted();
      });
    }
    connect(subsystemCombo, &QComboBox::activated, this, [this] {
      syncRouteFromUi();
      if (onSubsystemCommitted) onSubsystemCommitted();
    });
    for (auto combo : {rtCombo, saCombo, directionCombo, countCombo, channelCombo, busCombo, matchWordCombo})
      connect(combo, &QComboBox::currentIndexChanged, this, [this] { syncRouteFromUi(); });
    connect(matchValueEdit, &QLineEdit::textChanged, this, [this] { syncRouteFromUi(); });

    connect(addFieldBtn, &QPushButton::clicked, this, [this] { addField(true); });
    connect(bulkAddBtn, &QPushButton::clicked, this, [this] { addMultipleWordsDialog(); });
    connect(expandAllBtn, &QPushButton::clicked, this, [this] {
      for (auto card : cards) card->setExpanded(true);
    });
    connect(collapseAllBtn, &QPushButton::clicked, this, [this] {
      for (auto card : cards) card->setExpanded(false);
    });
  }

  void setSubsystems(QStringList names) {
    subsystems = std::move(names);
    const QSignalBlocker block(subsystemCombo);
    const auto text = subsystemCombo->currentText();
    subsystemCombo->clear();
    subsystemCombo->addItems(subsystems);
    subsystemCombo->setEditText(text);
  }

  void setOnModified(std::function<void()> callback) { onModified = std::move(callback); }
  void setOnSubsystemCommitted(std::function<void()> callback) { onSubsystemCommitted = std::move(callback); }
  QJsonObject command() const { return definition; }
  void focusSubsystem() { subsystemCombo->setFocus(); if (auto line = subsystemCombo->lineEdit()) line->selectAll(); }

  void load(const QJsonObject &data) {
    loading = true;
    definition = data;
    const bool hasCommand = !definition.isEmpty();
    routeCard->setEnabled(hasCommand);
    for (auto btn : {addFieldBtn, bulkAddBtn, expandAllBtn, collapseAllBtn}) btn->setEnabled(hasCommand);
    if (!hasCommand) {
      routeLabel->setText("Select or add a command to map fields (Word 0 to Word 31, 0-based).");
      qDeleteAll(cards);
      cards.clear();
      loading = false;
      return;
    }
    const int wc = qBound(1, definition["count"].toInt(32), 32);
    definition["count"] = wc;
    definition["word_base"] = 0;
    {
      const QSignalBlocker b1(subsystemCombo), b2(rtCombo), b3(saCombo), b4(directionCombo),
        b5(countCombo), b6(channelCombo), b7(busCombo), b8(matchValueEdit);
      subsystemCombo->setEditText(definition["subsystem"].toString());
      rtCombo->setCurrentIndex(qBound(0, definition["rt"].toInt(), 30));
      saCombo->setCurrentIndex(qBound(0, definition["sa"].toInt(1) - 1, 29));
      directionCombo->setCurrentIndex(definition["tx"].toBool() ? 1 : 0);
      countCombo->setCurrentIndex(wc - 1);
      channelCombo->setCurrentIndex(qBound(0, definition["channel"].toInt(), 2));
      busCombo->setCurrentIndex(qBound(0, definition["bus"].toInt(-1) + 1, 2));
      const int mw = definition.value("match_word").toInt(-1);
      updateTagWordChoices(wc, mw);
      const auto mv = definition.value("match_value");
      matchValueEdit->setText(mv.isDouble() ? QString::number(mv.toInt(), 16).toUpper() : mv.toString());
    }
    routeLabel->setText(desktop::manualRouteLabel(definition) + " · Words 0–" + QString::number(wc - 1) + " (0-based)");
    rebuildCards();
    loading = false;
  }

  void addField(bool focusNew = false) {
    if (definition.isEmpty()) return;
    auto entries = definition["fields"].toArray();
    if (entries.size() >= 512) return;
    const int wc = qBound(1, definition["count"].toInt(32), 32);
    int nextWord = 0;
    for (const auto &entry : entries) {
      const auto f = entry.toObject();
      nextWord = qMax(nextWord, f["word"].toInt() + (f["lsb"].toInt() + f["width"].toInt() + 15) / 16);
    }
    nextWord = qMin(nextWord, wc - 1);
    entries.append(QJsonObject{
      {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
      {"name", QString("Signal_%1").arg(nextWord)},
      {"word", nextWord},
      {"width", 16},
      {"lsb", 0},
      {"type", "unsigned"}
    });
    definition["fields"] = entries;
    rebuildCards(entries.size() - 1);
    if (focusNew && !cards.isEmpty()) {
      cards.back()->setExpanded(true);
      cardsScroll->ensureWidgetVisible(cards.back());
      cards.back()->focusName();
    }
    if (onModified) onModified();
  }

  void addMultipleWordsDialog() {
    if (definition.isEmpty()) return;
    const int wc = qBound(1, definition["count"].toInt(32), 32);
    auto entries = definition["fields"].toArray();
    int suggestedStart = 0;
    for (const auto &entry : entries) {
      const auto f = entry.toObject();
      suggestedStart = qMax(suggestedStart, f["word"].toInt() + (f["lsb"].toInt() + f["width"].toInt() + 15) / 16);
    }
    suggestedStart = qMin(suggestedStart, wc - 1);
    QDialog dlg(this);
    dlg.setWindowTitle("Add multiple word fields");
    auto form = new QFormLayout(&dlg);
    auto startSpin = new QSpinBox(&dlg);
    startSpin->setRange(0, wc - 1);
    startSpin->setValue(suggestedStart);
    auto totalSpin = new QSpinBox(&dlg);
    totalSpin->setRange(1, wc - suggestedStart);
    totalSpin->setValue(qMin(4, wc - suggestedStart));
    connect(startSpin, &QSpinBox::valueChanged, totalSpin, [totalSpin, wc](int s) {
      totalSpin->setRange(1, wc - s);
    });
    auto typeBox = new QComboBox(&dlg);
    typeBox->addItem("Unsigned (1 word)", "unsigned");
    typeBox->addItem("Signed (1 word)", "signed");
    typeBox->addItem("Float32 (2 words, LSW first)", "float32");
    form->addRow("Starting at Word (0-based)", startSpin);
    form->addRow("Number of fields", totalSpin);
    form->addRow("Default type", typeBox);
    auto btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btns);
    if (dlg.exec() != QDialog::Accepted) return;

    const auto type = typeBox->currentData().toString();
    const int step = type == "float32" ? 2 : 1;
    int word = startSpin->value();
    for (int k = 0; k < totalSpin->value() && word + step <= wc && entries.size() < 512; ++k, word += step) {
      entries.append(QJsonObject{
        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"name", QString("Signal_%1").arg(word)},
        {"word", word},
        {"width", step * 16},
        {"lsb", 0},
        {"type", type},
        {"order", step > 1 ? "LSW first" : QString()}
      });
    }
    definition["fields"] = entries;
    rebuildCards(-2);
    if (onModified) onModified();
  }

  desktop::ManualMappingResult validateExamples() {
    const auto result = desktop::checkManualMapping(definition);
    for (int row = 0; row < cards.size(); ++row) {
      const auto msg = result.fields.value(row, result.error);
      const bool ok = msg.startsWith("Pass") || msg.startsWith("Ready");
      cards[row]->setStatus(msg, ok);
    }
    return result;
  }

  void expandFailures() {
    const auto result = desktop::checkManualMapping(definition);
    for (int row = 0; row < cards.size(); ++row) {
      const auto msg = result.fields.value(row, result.error);
      if (!msg.startsWith("Pass") && !msg.startsWith("Ready")) cards[row]->setExpanded(true);
    }
  }
};
} // namespace

namespace desktop {
QJsonObject readManualDrafts() {
  QFile file(draftPath());
  if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) return {};
  auto obj = QJsonDocument::fromJson(file.readAll()).object();
  if (obj.contains("commands")) obj["commands"] = normalizeManualCommands(obj["commands"].toArray());
  return obj;
}

bool saveManualDrafts(const QJsonObject &data) {
  if (!QDir().mkpath(QFileInfo(draftPath()).absolutePath())) return false;
  auto normalized = data;
  if (normalized.contains("commands")) normalized["commands"] = normalizeManualCommands(normalized["commands"].toArray());
  const auto bytes = QJsonDocument(normalized).toJson(QJsonDocument::Compact);
  QSaveFile file(draftPath());
  return bytes.size() <= 16 * 1024 * 1024 && file.open(QIODevice::WriteOnly) &&
    file.write(bytes) == bytes.size() && file.commit();
}
} // namespace desktop

void Window::manualMappings() {
  navigate(12);
}

void Window::signalMappingsPage() {
  if (!pageLayout || !content) return;
  auto host = buildManualMappingsWidget(content, true);
  host->setProperty("designerSource", "manual_mappings.ui");
  pageLayout->addWidget(host, 1);
}

QWidget *Window::buildManualMappingsWidget(QWidget *parent, bool embedded) {
  auto host = new QWidget(parent);
  host->setObjectName(embedded ? "manualMappingHost" : "manualMappingDialog");
  Ui::ManualMappings form;
  form.setupUi(host);
  host->setObjectName(embedded ? "manualMappingHost" : "manualMappingDialog");
  host->setStyleSheet(
    "#manualMappingHost,#manualMappingDialog{background:#f3f7fb;}"
    "#manualMappingHost QLineEdit,#manualMappingHost QComboBox,#manualMappingHost QSpinBox,"
    "#manualMappingDialog QLineEdit,#manualMappingDialog QComboBox,#manualMappingDialog QSpinBox{"
    "  padding:4px 8px;min-height:26px;font-size:13px;border-radius:6px;border:1px solid #cddae4;background:#ffffff;}"
    "#manualMappingHost QCheckBox,#manualMappingDialog QCheckBox{font-size:13px;padding:3px 0;}"
    "#manualMappingHost QPushButton,#manualMappingDialog QPushButton{border-radius:8px;}"
    "#manualMappingHost QPushButton[compactAction=\"true\"],#manualMappingDialog QPushButton[compactAction=\"true\"]{"
    "  padding:4px 10px;min-height:24px;font-size:12px;border-radius:7px;}"
    "#commandsPanel{background:#ffffff;border:1px solid #d4e0e9;border-radius:10px;}"
    "#commandsPanel QPushButton{margin:0 10px;padding:7px 12px;font-size:13px;font-weight:600;}"
    "#manualCommands{border:0;background:transparent;outline:0;padding:4px 0;border-top-left-radius:10px;border-top-right-radius:10px;}"
    "#manualCommands QHeaderView::section{background:#eef6f2;color:#142d3a;font-weight:700;font-size:13px;padding:10px 12px;border:0;border-bottom:1px solid #d4e6dd;border-top-left-radius:10px;border-top-right-radius:10px;}"
    "#manualCommands::item{padding:7px 8px;margin:2px 6px;border-radius:7px;font-size:13px;color:#172b3a;}"
    "#manualCommands::item:hover{background:#f0f7f3;}"
    "#manualCommands::item:selected{background:#e3f3ea;color:#124d32;}"
    "#manualCommands::indicator{width:16px;height:16px;border-radius:4px;border:1px solid #9bb0bf;background:#ffffff;}"
    "#manualCommands::indicator:hover{border-color:#146343;}"
    "#manualCommands::indicator:checked{background:#146343;border-color:#146343;image:url(:/icons/check.svg);}"
    "#manualCommands::indicator:indeterminate{background:#146343;border-color:#146343;}"
    "#manualRouteCard{background:#ffffff;border:1px solid #d4e0e9;border-radius:10px;}"
    "#manualFieldCard{background:#ffffff;border:1px solid #d4e0e9;border-radius:10px;}"
    "#manualFieldHeader{background:#f2f7f5;border-bottom:1px solid #e4ece8;border-top-left-radius:10px;border-top-right-radius:10px;}"
    "#manualStatus{background:#eef6f1;border:1px solid #cde2d5;border-radius:8px;padding:8px 12px;font-size:13px;color:#1b432e;}"
  );
  auto hostLayout = qobject_cast<QVBoxLayout *>(host->layout());
  if (embedded && hostLayout) {
    hostLayout->setContentsMargins(0, 0, 0, 0);
    auto topBar = new QWidget(host);
    auto topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(0, 0, 0, 4);
    topLayout->setSpacing(8);
    auto importBtn = new QPushButton("Import JSON/CSV…", topBar);
    importBtn->setObjectName("importIcdButton");
    desktop::actionIcon(importBtn, "add-file");
    auto exportBtn = new QPushButton("Export Profile JSON…", topBar);
    exportBtn->setObjectName("exportProfileButton");
    desktop::actionIcon(exportBtn, "download");
    auto liveBtn = new QPushButton("Live Engineering", topBar);
    liveBtn->setObjectName("openLiveEngineeringButton");
    desktop::actionIcon(liveBtn, "chart");
    auto nextBtn = new QPushButton("→ Next: Engineering && Plots →", topBar);
    nextBtn->setObjectName("nextToEngineeringButton");
    nextBtn->setProperty("primary", true);
    topLayout->addWidget(importBtn, 1);
    topLayout->addWidget(exportBtn, 1);
    topLayout->addWidget(liveBtn, 1);
    topLayout->addWidget(nextBtn, 1);
    hostLayout->insertWidget(0, topBar);
    hostLayout->setStretch(1, 1);

    connect(importBtn, &QPushButton::clicked, host, [this, host] {
      const auto path = QFileDialog::getOpenFileName(host, "Import signal profile", sessionDir, "Signal profiles (*.json *.csv)");
      if (!path.isEmpty()) importIcdPath(path);
    });
    connect(exportBtn, &QPushButton::clicked, host, [this, host] {
      const auto path = QFileDialog::getSaveFileName(host, "Export active signal profile", sessionDir + "/icd-profile.json", "JSON (*.json)");
      if (path.isEmpty()) return;
      QSaveFile f(path);
      if (!f.open(QIODevice::WriteOnly)) { toast(f.errorString()); return; }
      const auto doc = QJsonDocument(desktop::profileToJson(backend.profile)).toJson(QJsonDocument::Indented);
      if (f.write(doc) != doc.size() || !f.commit()) { toast("Profile export failed."); return; }
      toast("Exported active signal profile JSON.");
    });
    connect(liveBtn, &QPushButton::clicked, this, [this] { liveEngineering(); });
    connect(nextBtn, &QPushButton::clicked, this, [this] { navigate(6); });
  } else if (hostLayout) {
    hostLayout->setStretch(0, 1);
  }
  auto dialog = host;

  form.commandsLayout->setContentsMargins(0,0,0,10);
  form.commandsLayout->setSpacing(6);
  form.manualCommands->header()->setSectionResizeMode(0,QHeaderView::Stretch);
  form.manualCommands->setWordWrap(true);
  form.manualCommands->setUniformRowHeights(false);
  form.manualCommands->setIndentation(16);
  form.manualCommands->setRootIsDecorated(true);
  auto editor = new CommandEditor(form.detailsPanel);
  form.detailsLayout->addWidget(editor);
  form.workspaceSplit->setStretchFactor(0, 0);
  form.workspaceSplit->setStretchFactor(1, 1);
  form.workspaceSplit->setSizes({315, 885});
  form.commandsPanel->setMinimumWidth(240);

  auto commands = std::make_shared<QJsonArray>(desktop::readManualDrafts()["commands"].toArray());
  if (commands->isEmpty()) *commands = desktop::normalizeManualCommands(manualRegistry["commands"].toArray());
  auto current = std::make_shared<int>(-1);

  const auto formatTreeCommandLabel = [](const QJsonObject &cmd) {
    auto route = desktop::manualRouteLabel(cmd);
    const auto sep = route.lastIndexOf(QStringLiteral(" · "));
    const QString head = (sep > 0 ? route.left(sep) : route).replace(QStringLiteral(" / "), QStringLiteral(" · "));
    const QString hex = sep > 0 ? route.mid(sep + 3) : QString();
    const int n = cmd["fields"].toArray().size();
    const QString fieldText = n == 1 ? QStringLiteral("1 field") : QStringLiteral("%1 fields").arg(n);
    return hex.isEmpty() ? QStringLiteral("%1\n%2").arg(head, fieldText)
                         : QStringLiteral("%1\n%2 · %3").arg(head, hex, fieldText);
  };

  const auto synchronize = [commands, current, editor] {
    if (*current < 0 || *current >= commands->size()) return;
    auto data = editor->command();
    data["active"] = commands->at(*current).toObject()["active"].toBool();
    commands->replace(*current, data);
  };

  auto rebuildingTree = std::make_shared<bool>(false);
  const auto updateCurrentTreeLabel = [form, current, commands, rebuildingTree, formatTreeCommandLabel] {
    if (*rebuildingTree || *current < 0 || *current >= commands->size()) return;
    if (auto item = form.manualCommands->currentItem(); item && item->data(0, Qt::UserRole).toInt() == *current) {
      const auto cmd = commands->at(*current).toObject();
      item->setText(0, formatTreeCommandLabel(cmd));
      item->setToolTip(0, desktop::manualRouteLabel(cmd));
    }
  };

  const auto rebuildTreeOnly = [form, commands, current, rebuildingTree, formatTreeCommandLabel](int row) {
    *rebuildingTree = true;
    const QSignalBlocker block(form.manualCommands);
    form.manualCommands->clear();
    QMap<QString, QTreeWidgetItem *> groups;
    QTreeWidgetItem *selected = nullptr;
    for (int index = 0; index < commands->size(); ++index) {
      const auto command = commands->at(index).toObject();
      const auto group = command["subsystem"].toString().trimmed();
      const auto name = group.isEmpty() ? QString("Unnamed subsystem") : group;
      auto &node = groups[name];
      if (!node) {
        node = new QTreeWidgetItem(form.manualCommands, {name});
        node->setData(0, Qt::UserRole, -1);
        node->setFlags(node->flags() | Qt::ItemIsAutoTristate | Qt::ItemIsUserCheckable);
        node->setCheckState(0, Qt::Unchecked);
        auto groupFont = node->font(0);
        groupFont.setWeight(QFont::DemiBold);
        node->setFont(0, groupFont);
        node->setForeground(0, QColor("#143627"));
      }
      auto item = new QTreeWidgetItem(node, {formatTreeCommandLabel(command)});
      item->setToolTip(0, desktop::manualRouteLabel(command));
      item->setData(0, Qt::UserRole, index);
      item->setCheckState(0, command["active"].toBool() ? Qt::Checked : Qt::Unchecked);
      if (index == row) selected = item;
    }
    form.manualCommands->expandAll();
    *current = selected ? row : -1;
    form.manualCommands->setCurrentItem(selected);
    *rebuildingTree = false;
    return selected != nullptr;
  };

  const auto rebuild = [rebuildTreeOnly, current, editor, commands](int row) {
    const bool ok = rebuildTreeOnly(row);
    editor->setEnabled(ok);
    editor->load(ok ? commands->at(row).toObject() : QJsonObject{});
  };

  const auto setSubsystems = [this, editor, commands] {
    QStringList names;
    for (const auto &signal : backend.profile)
      if (!signal.subsystem.empty()) names << QString::fromStdString(signal.subsystem);
    for (const auto &entry : *commands) {
      const auto name = entry.toObject()["subsystem"].toString();
      if (!name.isEmpty()) names << name;
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    editor->setSubsystems(names);
  };

  editor->setOnModified([synchronize, updateCurrentTreeLabel] {
    synchronize();
    updateCurrentTreeLabel();
  });
  editor->setOnSubsystemCommitted([synchronize, rebuildTreeOnly, current] {
    synchronize();
    if (*current >= 0) rebuildTreeOnly(*current);
  });

  connect(form.manualCommands, &QTreeWidget::itemChanged, dialog, [commands](QTreeWidgetItem *item, int) {
    const auto index = item->data(0, Qt::UserRole).toInt();
    if (index < 0 || index >= commands->size()) return;
    auto data = commands->at(index).toObject();
    data["active"] = item->checkState(0) == Qt::Checked;
    commands->replace(index, data);
  });

  connect(form.manualCommands, &QTreeWidget::currentItemChanged, dialog, [editor, current, commands, synchronize](QTreeWidgetItem *item) {
    synchronize();
    *current = item ? item->data(0, Qt::UserRole).toInt() : -1;
    editor->setEnabled(*current >= 0);
    editor->load(*current >= 0 ? commands->at(*current).toObject() : QJsonObject{});
  });

  auto removeCommandBtn = new QPushButton("Remove command", form.commandsPanel);
  removeCommandBtn->setObjectName("removeManualCommand");
  form.commandsLayout->addWidget(removeCommandBtn);
  desktop::actionIcon(form.addManualCommand, "add-file");
  desktop::actionIcon(removeCommandBtn, "cancel");
  desktop::actionIcon(form.saveManualDraft, "save");
  desktop::actionIcon(form.validateManual, "check");
  desktop::actionIcon(form.publishManual, "verified", true);

  const auto addNewCommand = [this, commands, current, rebuild, setSubsystems, editor, form] {
    setSubsystems();
    const auto subsystem = !workspaceSubsystem.isEmpty() ? workspaceSubsystem : QString("Subsystem-1");
    QJsonObject command{
      {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
      {"word_base", 0},
      {"subsystem", subsystem},
      {"rt", 1},
      {"sa", 1},
      {"tx", true},
      {"count", 32},
      {"channel", 0},
      {"bus", -1},
      {"match_word", -1},
      {"active", true},
      {"fields", QJsonArray{QJsonObject{
        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"name", "Signal_0"},
        {"word", 0},
        {"width", 16},
        {"lsb", 0},
        {"type", "unsigned"}
      }}}
    };
    commands->append(command);
    rebuild(commands->size() - 1);
    editor->focusSubsystem();
    form.manualStatus->setText("Added new command (0-based Word 0 to Word 31). Edit route/fields or add more words, then Publish.");
  };

  connect(form.addManualCommand, &QPushButton::clicked, dialog, addNewCommand);
  connect(removeCommandBtn, &QPushButton::clicked, dialog, [commands, current, rebuild, form, dialog] {
    if (*current < 0 || *current >= commands->size()) { form.manualStatus->setText("Select a command."); return; }
    if (QMessageBox::question(dialog, "Remove command", "Remove the selected command from manual mappings?",
          QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    commands->removeAt(*current);
    rebuild(commands->isEmpty() ? -1 : qMin(*current, commands->size() - 1));
    form.manualStatus->setText("Command removed from draft.");
  });

  connect(form.validateManual, &QPushButton::clicked, dialog, [editor, form, current, synchronize] {
    synchronize();
    if (*current < 0) { form.manualStatus->setText("Select a command."); return; }
    const auto result = editor->validateExamples();
    if (!result.error.isEmpty()) editor->expandFailures();
    form.manualStatus->setText(result.error.isEmpty()
      ? QString("All %1 fields valid and ready to publish.").arg(result.fields.size())
      : result.error);
  });

  connect(form.saveManualDraft, &QPushButton::clicked, dialog, [form, current, commands, synchronize, rebuild] {
    synchronize();
    const auto saved = desktop::saveManualDrafts({{"commands", *commands}});
    rebuild(*current);
    form.manualStatus->setText(saved ? "Drafts saved." : "Could not save your drafts.");
  });

  connect(form.publishManual, &QPushButton::clicked, dialog, [this, form, editor, current, commands, synchronize, rebuild] {
    synchronize();
    std::vector<aero::Signal> profile;
    for (const auto &signal : backend.profile)
      if (signal.icdSetId != "manual") profile.push_back(signal);
    QJsonArray approved;
    const auto revision = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto previouslyApproved = desktop::normalizeManualCommands(manualRegistry["commands"].toArray());
    for (int index = 0; index < commands->size(); ++index) {
      const auto command = commands->at(index).toObject();
      if (!command["active"].toBool()) {
        for (const auto &old : previouslyApproved) if (old.toObject()["id"] == command["id"]) {
          auto inactive = old.toObject();
          inactive["active"] = false;
          approved.append(inactive);
          break;
        }
        continue;
      }
      auto verified = desktop::checkManualMapping(command);
      if (!verified.error.isEmpty()) {
        if (index != *current) rebuild(index);
        editor->validateExamples();
        editor->expandFailures();
        form.manualStatus->setText(QString("%1: %2").arg(desktop::manualRouteLabel(command), verified.error));
        return;
      }
      const auto &representative = verified.rules.front();
      if (std::any_of(profile.begin(), profile.end(), [&](const auto &signal) {
            return desktop::overlappingManualRoute(representative, signal);
          })) {
        if (index != *current) rebuild(index);
        form.manualStatus->setText(QString("%1 overlaps another active route/tag. Change the route or set a Frame tag word.")
          .arg(desktop::manualRouteLabel(command)));
        return;
      }
      for (auto &signal : verified.rules) {
        signal.icdRevision = revision.toStdString();
        profile.push_back(std::move(signal));
      }
      approved.append(command);
    }
    auto history = manualRegistry["history"].toArray();
    if (!manualRegistry["revision"].toString().isEmpty())
      history.append(QJsonObject{
        {"revision", manualRegistry["revision"]},
        {"utc", manualRegistry["utc"]},
        {"commands", manualRegistry["commands"]}
      });
    const QJsonObject library{
      {"schema", "aerotrace.manual-mappings.v1"},
      {"commands", approved},
      {"revision", revision},
      {"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
      {"history", history}
    };
    if (!desktop::saveManualDrafts({{"commands", *commands}})) {
      form.manualStatus->setText("Could not save the drafts. Publication cancelled.");
      return;
    }
    const auto error = publishProfile(profile, &library);
    if (!error.isEmpty()) {
      form.manualStatus->setText(error);
      return;
    }
    rebuild(*current);
    form.manualStatus->setText(QString("Published %1 active field(s).").arg(profile.size()));
    if (page == 12 || page == 6 || page == 7) navigate(page);
  });

  form.manualStatus->setText("Word 0 to Word 31 (0-based). Expand any fields to edit together; hex test examples are optional.");
  setSubsystems();
  if (pendingPreFillRoute.valid) {
    const auto route = pendingPreFillRoute;
    pendingPreFillRoute = {};
    int matchIdx = -1;
    for (int i = 0; i < commands->size(); ++i) {
      const auto cmd = commands->at(i).toObject();
      if (cmd["rt"].toInt(-1) == route.rt && cmd["sa"].toInt(-1) == route.sa && cmd["tx"].toBool() == route.tx) {
        matchIdx = i;
        break;
      }
    }
    const QString sampleHex = route.hasSample ? QString("%1").arg(route.sampleWords[0], 4, 16, QChar('0')).toUpper() : QString();
    if (matchIdx < 0) {
      QJsonObject field{
        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"name", QString("RT%1_SA%2_W0").arg(route.rt).arg(route.sa)},
        {"word", 0},
        {"width", 16},
        {"lsb", 0},
        {"type", "unsigned"}
      };
      if (!sampleHex.isEmpty()) field["example_hex"] = sampleHex;
      const QString sub = !route.subsystem.isEmpty() ? route.subsystem
        : (!workspaceSubsystem.isEmpty() ? workspaceSubsystem : QString("Captured Routes"));
      QJsonObject command{
        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"word_base", 0},
        {"subsystem", sub},
        {"rt", route.rt},
        {"sa", route.sa},
        {"tx", route.tx},
        {"count", qBound(1, route.wordCount, 32)},
        {"channel", route.channel},
        {"bus", route.bus},
        {"match_word", -1},
        {"active", true},
        {"fields", QJsonArray{field}}
      };
      commands->append(command);
      matchIdx = commands->size() - 1;
    } else if (!sampleHex.isEmpty()) {
      auto cmd = commands->at(matchIdx).toObject();
      auto fields = cmd["fields"].toArray();
      if (!fields.isEmpty()) {
        auto first = fields[0].toObject();
        if (first["example_hex"].toString().trimmed().isEmpty()) {
          first["example_hex"] = sampleHex;
          fields[0] = first;
          cmd["fields"] = fields;
          commands->replace(matchIdx, cmd);
        }
      }
    }
    setSubsystems();
    rebuild(matchIdx);
    editor->expandFailures();
    form.manualStatus->setText(QString("Pre-filled RT %1 / SA %2 / %3 (%4 words%5). Adjust Word 0–31 fields and click Publish selected commands.")
      .arg(route.rt).arg(route.sa).arg(route.tx ? "Tx" : "Rx").arg(route.wordCount)
      .arg(sampleHex.isEmpty() ? QString() : QString(", W0=0x%1").arg(sampleHex)));
  } else if (commands->isEmpty() && !qApp->property("recordingFixtureUi").toBool()) {
    addNewCommand();
  } else {
    rebuild(commands->isEmpty() ? -1 : 0);
  }
  return host;
}
