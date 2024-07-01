// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/toolwidgets/gradientsettings.h"
#include "desktop/widgets/groupedtoolbutton.h"
#include "desktop/widgets/kis_slider_spin_box.h"
#include "libclient/canvas/blendmodes.h"
#include "libclient/tools/gradient.h"
#include "libclient/tools/toolcontroller.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QWidget>

namespace tools {

namespace props {
static const ToolProperties::RangedValue<int> gradient{
	QStringLiteral("gradient"), 0, 0, 3},
	fgOpacity{QStringLiteral("fgOpacity"), 100, 0, 100},
	bgOpacity{QStringLiteral("bgOpacity"), 100, 0, 100},
	shape{QStringLiteral("shape"), 0, 0, 1},
	focus{QStringLiteral("focus"), 0, 0, 10000},
	spread{QStringLiteral("spread"), 0, 0, 2},
	blendMode{
		QStringLiteral("blendMode"), DP_BLEND_MODE_NORMAL, 0,
		DP_BLEND_MODE_MAX};
}

GradientSettings::GradientSettings(ToolController *ctrl, QObject *parent)
	: ToolSettings(ctrl, parent)
	, m_colorDebounce(100)
{
}

ToolProperties GradientSettings::saveToolSettings()
{
	ToolProperties cfg(toolType());
	cfg.setValue(props::gradient, m_gradientGroup->checkedId());
	cfg.setValue(props::fgOpacity, m_fgOpacitySpinner->value());
	cfg.setValue(props::bgOpacity, m_bgOpacitySpinner->value());
	cfg.setValue(props::shape, m_shapeGroup->checkedId());
	cfg.setValue(props::focus, qRound(m_focusSpinner->value() * 100.0));
	cfg.setValue(props::spread, m_spreadGroup->checkedId());
	cfg.setValue(props::blendMode, m_blendModeCombo->currentData().toInt());
	return cfg;
}

void GradientSettings::restoreToolSettings(const ToolProperties &cfg)
{
	checkGroupButton(m_gradientGroup, cfg.value(props::gradient));
	m_fgOpacitySpinner->setValue(cfg.value(props::fgOpacity));
	m_bgOpacitySpinner->setValue(cfg.value(props::bgOpacity));
	checkGroupButton(m_shapeGroup, cfg.value(props::shape));
	m_focusSpinner->setValue(cfg.value(props::focus) / 100.0);
	checkGroupButton(m_spreadGroup, cfg.value(props::spread));
	selectBlendMode(cfg.value(props::blendMode));
}

void GradientSettings::pushSettings()
{
	ToolController *ctrl = controller();
	GradientTool *tool =
		static_cast<GradientTool *>(ctrl->getTool(Tool::GRADIENT));

	int fgAlpha = qRound(m_fgOpacitySpinner->value() / 100.0 * 255.0);
	int bgAlpha = qRound(m_bgOpacitySpinner->value() / 100.0 * 255.0);
	QColor color1, color2;
	bool haveBackground;
	int gradient = m_gradientGroup->checkedId();
	switch(gradient) {
	case int(Gradient::ForegroundToTransparent):
		color1 = ctrl->foregroundColor();
		color1.setAlpha(fgAlpha);
		color2 = Qt::transparent;
		haveBackground = false;
		break;
	case int(Gradient::TransparentToForeground):
		color1 = Qt::transparent;
		color2 = ctrl->foregroundColor();
		color2.setAlpha(fgAlpha);
		haveBackground = false;
		break;
	case int(Gradient::ForegroundToBackground):
		color1 = ctrl->foregroundColor();
		color1.setAlpha(fgAlpha);
		color2 = ctrl->backgroundColor();
		color2.setAlpha(bgAlpha);
		haveBackground = true;
		break;
	case int(Gradient::BackgroundToForeground):
		color1 = ctrl->backgroundColor();
		color1.setAlpha(bgAlpha);
		color2 = ctrl->foregroundColor();
		color2.setAlpha(fgAlpha);
		haveBackground = true;
		break;
	default:
		qWarning("Unknown gradient %d", gradient);
		return;
	}

	GradientTool::Shape shape = GradientTool::Shape(m_shapeGroup->checkedId());
	tool->setParameters(
		color1, color2, shape, GradientTool::Spread(m_spreadGroup->checkedId()),
		m_focusSpinner->value() / 100.0,
		m_blendModeCombo->currentData().toInt());

	m_fgOpacitySpinner->setPrefix(
		haveBackground ? tr("Foreground: ") : tr("Opacity: "));
	m_bgOpacitySpinner->setEnabled(haveBackground);
	m_bgOpacitySpinner->setVisible(haveBackground);
	m_focusSpinner->setEnabled(shape == GradientTool::Shape::Radial);
	m_focusSpinner->setVisible(shape == GradientTool::Shape::Radial);
}

QWidget *GradientSettings::createUiWidget(QWidget *parent)
{
	m_headerWidget = new QWidget(parent);
	QHBoxLayout *headerLayout = new QHBoxLayout(m_headerWidget);
	headerLayout->setContentsMargins(0, 0, 0, 0);
	headerLayout->setSpacing(0);
	headerLayout->addStretch();

	widgets::GroupedToolButton *fgToTransparentButton =
		new widgets::GroupedToolButton(widgets::GroupedToolButton::GroupLeft);
	fgToTransparentButton->setCheckable(true);
	fgToTransparentButton->setChecked(true);
	fgToTransparentButton->setStatusTip(tr("Foreground color to transparency"));
	fgToTransparentButton->setToolTip(fgToTransparentButton->statusTip());
	fgToTransparentButton->setIcon(
		QIcon::fromTheme("drawpile_gradientfgtoalpha"));
	headerLayout->addWidget(fgToTransparentButton);

	widgets::GroupedToolButton *transparentToFgButton =
		new widgets::GroupedToolButton(widgets::GroupedToolButton::GroupCenter);
	transparentToFgButton->setCheckable(true);
	transparentToFgButton->setStatusTip(tr("Transparency to foreground color"));
	transparentToFgButton->setToolTip(transparentToFgButton->statusTip());
	transparentToFgButton->setIcon(
		QIcon::fromTheme("drawpile_gradientalphatofg"));
	headerLayout->addWidget(transparentToFgButton);

	widgets::GroupedToolButton *fgToBgButton =
		new widgets::GroupedToolButton(widgets::GroupedToolButton::GroupCenter);
	fgToBgButton->setCheckable(true);
	fgToBgButton->setStatusTip(tr("Foreground color to background color"));
	fgToBgButton->setToolTip(fgToBgButton->statusTip());
	fgToBgButton->setIcon(QIcon::fromTheme("drawpile_gradientfgtobg"));
	headerLayout->addWidget(fgToBgButton);

	widgets::GroupedToolButton *bgToFgButton =
		new widgets::GroupedToolButton(widgets::GroupedToolButton::GroupRight);
	bgToFgButton->setCheckable(true);
	bgToFgButton->setStatusTip(tr("Background color to foreground color"));
	bgToFgButton->setToolTip(bgToFgButton->statusTip());
	bgToFgButton->setIcon(QIcon::fromTheme("drawpile_gradientbgtofg"));
	headerLayout->addWidget(bgToFgButton);

	m_gradientGroup = new QButtonGroup(this);
	m_gradientGroup->addButton(
		fgToTransparentButton, int(Gradient::ForegroundToTransparent));
	m_gradientGroup->addButton(
		transparentToFgButton, int(Gradient::TransparentToForeground));
	m_gradientGroup->addButton(
		fgToBgButton, int(Gradient::ForegroundToBackground));
	m_gradientGroup->addButton(
		bgToFgButton, int(Gradient::BackgroundToForeground));
	connect(
		m_gradientGroup,
		QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this,
		&GradientSettings::pushSettings);

	headerLayout->addStretch();

	QWidget *widget = new QWidget(parent);
	QFormLayout *layout = new QFormLayout(widget);

	m_fgOpacitySpinner = new KisSliderSpinBox;
	m_fgOpacitySpinner->setRange(0, 100);
	m_fgOpacitySpinner->setValue(100);
	m_fgOpacitySpinner->setBlockUpdateSignalOnDrag(true);
	m_fgOpacitySpinner->setSuffix(tr("%"));
	layout->addRow(m_fgOpacitySpinner);
	connect(
		m_fgOpacitySpinner, QOverload<int>::of(&KisSliderSpinBox::valueChanged),
		this, &GradientSettings::pushSettings);

	m_bgOpacitySpinner = new KisSliderSpinBox;
	m_bgOpacitySpinner->setRange(0, 100);
	m_bgOpacitySpinner->setValue(100);
	m_bgOpacitySpinner->setBlockUpdateSignalOnDrag(true);
	m_bgOpacitySpinner->setPrefix(tr("Background: "));
	m_bgOpacitySpinner->setSuffix(tr("%"));
	layout->addRow(m_bgOpacitySpinner);
	connect(
		m_bgOpacitySpinner, QOverload<int>::of(&KisSliderSpinBox::valueChanged),
		this, &GradientSettings::pushSettings);

	widgets::GroupedToolButton *linearButton =
		new widgets::GroupedToolButton(widgets::GroupedToolButton::GroupLeft);
	linearButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	linearButton->setText(tr("Linear"));
	linearButton->setStatusTip(tr("Straight gradient shape"));
	linearButton->setToolTip(linearButton->statusTip());
	linearButton->setCheckable(true);
	linearButton->setChecked(true);

	widgets::GroupedToolButton *radialButton =
		new widgets::GroupedToolButton(widgets::GroupedToolButton::GroupRight);
	radialButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	radialButton->setText(tr("Radial"));
	radialButton->setStatusTip(tr("Circular gradient shape"));
	radialButton->setToolTip(radialButton->statusTip());
	radialButton->setCheckable(true);

	m_shapeGroup = new QButtonGroup(this);
	m_shapeGroup->addButton(linearButton, int(GradientTool::Shape::Linear));
	m_shapeGroup->addButton(radialButton, int(GradientTool::Shape::Radial));
	connect(
		m_shapeGroup,
		QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this,
		&GradientSettings::pushSettings);

	QHBoxLayout *shapeLayout = new QHBoxLayout;
	shapeLayout->setContentsMargins(0, 0, 0, 0);
	shapeLayout->setSpacing(0);
	shapeLayout->addWidget(linearButton);
	shapeLayout->addWidget(radialButton);
	layout->addRow(tr("Shape:"), shapeLayout);

	m_focusSpinner = new KisDoubleSliderSpinBox;
	m_focusSpinner->setRange(0.0, 100.0);
	m_focusSpinner->setValue(0.0);
	m_focusSpinner->setBlockUpdateSignalOnDrag(true);
	m_focusSpinner->setDecimals(2);
	m_focusSpinner->setPrefix(tr("Focus: "));
	m_focusSpinner->setSuffix(tr("%"));
	layout->addRow(m_focusSpinner);
	connect(
		m_focusSpinner,
		QOverload<double>::of(&KisDoubleSliderSpinBox::valueChanged), this,
		&GradientSettings::pushSettings);

	widgets::GroupedToolButton *padButton =
		new widgets::GroupedToolButton(widgets::GroupedToolButton::GroupLeft);
	padButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	padButton->setText(tr("Pad"));
	padButton->setStatusTip(
		tr("Continue gradient by padding it with the nearest color"));
	padButton->setToolTip(padButton->statusTip());
	padButton->setCheckable(true);
	padButton->setChecked(true);

	widgets::GroupedToolButton *repeatButton =
		new widgets::GroupedToolButton(widgets::GroupedToolButton::GroupCenter);
	repeatButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	repeatButton->setText(tr("Repeat"));
	repeatButton->setStatusTip(
		tr("Continue gradient by repeating it from the beginning"));
	repeatButton->setToolTip(repeatButton->statusTip());
	repeatButton->setCheckable(true);

	widgets::GroupedToolButton *reflectButton =
		new widgets::GroupedToolButton(widgets::GroupedToolButton::GroupRight);
	reflectButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	reflectButton->setText(tr("Reflect"));
	reflectButton->setStatusTip(tr("Continue gradient by reflecting it"));
	reflectButton->setToolTip(reflectButton->statusTip());
	reflectButton->setCheckable(true);

	m_spreadGroup = new QButtonGroup(this);
	m_spreadGroup->addButton(padButton, int(GradientTool::Spread::Pad));
	m_spreadGroup->addButton(repeatButton, int(GradientTool::Spread::Repeat));
	m_spreadGroup->addButton(reflectButton, int(GradientTool::Spread::Reflect));
	connect(
		m_spreadGroup,
		QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this,
		&GradientSettings::pushSettings);

	QHBoxLayout *spreadLayout = new QHBoxLayout;
	spreadLayout->setContentsMargins(0, 0, 0, 0);
	spreadLayout->setSpacing(0);
	spreadLayout->addWidget(padButton);
	spreadLayout->addWidget(repeatButton);
	spreadLayout->addWidget(reflectButton);
	layout->addRow(tr("Spread:"), spreadLayout);

	m_blendModeCombo = new QComboBox;
	initBlendModeOptions();
	layout->addRow(tr("Mode:"), m_blendModeCombo);
	connect(
		m_blendModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &GradientSettings::pushSettings);

	m_applyButton = new QPushButton(
		widget->style()->standardIcon(QStyle::SP_DialogApplyButton),
		tr("Apply"));
	m_applyButton->setStatusTip(tr("Apply the gradient"));
	m_applyButton->setToolTip(m_applyButton->statusTip());
	m_applyButton->setEnabled(false);
	connect(
		m_applyButton, &QPushButton::clicked, controller(),
		&ToolController::finishMultipartDrawing);

	m_cancelButton = new QPushButton(
		widget->style()->standardIcon(QStyle::SP_DialogCancelButton),
		tr("Cancel"));
	m_cancelButton->setStatusTip(tr("Discard the gradient"));
	m_cancelButton->setToolTip(m_cancelButton->statusTip());
	m_cancelButton->setEnabled(false);
	connect(
		m_cancelButton, &QPushButton::clicked, controller(),
		&ToolController::cancelMultipartDrawing);

	QHBoxLayout *applyCancelLayout = new QHBoxLayout;
	applyCancelLayout->setContentsMargins(0, 0, 0, 0);
	applyCancelLayout->addWidget(m_applyButton);
	applyCancelLayout->addWidget(m_cancelButton);
	layout->addRow(applyCancelLayout);

	connect(
		&m_colorDebounce, &DebounceTimer::noneChanged, this,
		&GradientSettings::pushSettings);

	return widget;
}

void GradientSettings::checkGroupButton(QButtonGroup *group, int id)
{
	QAbstractButton *button = group->button(id);
	if(button) {
		button->setChecked(true);
	}
}

void GradientSettings::updateColor()
{
	m_colorDebounce.setNone();
}

void GradientSettings::initBlendModeOptions()
{
	int selectedBlendMode = m_blendModeCombo->count() == 0
								? DP_BLEND_MODE_NORMAL
								: m_blendModeCombo->currentData().toInt();
	{
		QSignalBlocker blocker(m_blendModeCombo);
		m_blendModeCombo->clear();
		for(const canvas::blendmode::Named &named :
			canvas::blendmode::pasteModeNames()) {
			if(!m_compatibilityMode ||
			   canvas::blendmode::isBackwardCompatibleMode(named.mode)) {
				m_blendModeCombo->addItem(named.name, int(named.mode));
			}
		}
	}
	selectBlendMode(
		!m_compatibilityMode || canvas::blendmode::isBackwardCompatibleMode(
									DP_BlendMode(selectedBlendMode))
			? selectedBlendMode
			: DP_BLEND_MODE_NORMAL);
}

void GradientSettings::selectBlendMode(int blendMode)
{
	int count = m_blendModeCombo->count();
	for(int i = 0; i < count; ++i) {
		if(m_blendModeCombo->itemData(i).toInt() == blendMode) {
			m_blendModeCombo->setCurrentIndex(i);
			break;
		}
	}
}

}
