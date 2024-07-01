// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DESKTOP_TOOLWIDGETS_GRADIENTSETTINGS_H
#define DESKTOP_TOOLWIDGETS_GRADIENTSETTINGS_H
#include "desktop/toolwidgets/toolsettings.h"
#include "libclient/utils/debouncetimer.h"

class KisDoubleSliderSpinBox;
class KisSliderSpinBox;
class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;

namespace tools {

class GradientSettings final : public ToolSettings {
	Q_OBJECT
public:
	GradientSettings(ToolController *ctrl, QObject *parent = nullptr);

	QString toolType() const override { return QStringLiteral("gradient"); }

	bool affectsCanvas() override { return true; }
	bool affectsLayer() override { return true; }

	void setForeground(const QColor &) override { updateColor(); }
	void setBackground(const QColor &) override { updateColor(); }

	ToolProperties saveToolSettings() override;
	void restoreToolSettings(const ToolProperties &cfg) override;

	void pushSettings() override;

	QWidget *getHeaderWidget() override { return m_headerWidget; }

protected:
	QWidget *createUiWidget(QWidget *parent) override;

private:
	enum class Gradient {
		ForegroundToTransparent,
		TransparentToForeground,
		ForegroundToBackground,
		BackgroundToForeground,
	};

	static void checkGroupButton(QButtonGroup *group, int id);

	void updateColor();

	void initBlendModeOptions();
	void selectBlendMode(int blendMode);

	QWidget *m_headerWidget = nullptr;
	QButtonGroup *m_gradientGroup = nullptr;
	KisSliderSpinBox *m_fgOpacitySpinner = nullptr;
	KisSliderSpinBox *m_bgOpacitySpinner = nullptr;
	QButtonGroup *m_shapeGroup = nullptr;
	KisDoubleSliderSpinBox *m_focusSpinner = nullptr;
	QButtonGroup *m_spreadGroup = nullptr;
	QComboBox *m_blendModeCombo = nullptr;
	QPushButton *m_applyButton = nullptr;
	QPushButton *m_cancelButton = nullptr;
	bool m_compatibilityMode = false;
	DebounceTimer m_colorDebounce;
};

}

#endif
