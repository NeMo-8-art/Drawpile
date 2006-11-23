/*
   DrawPile - a collaborative drawing program.

   Copyright (C) 2006 Calle Laakkonen

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QSettings>

#include "mainwindow.h"
#include "netstatus.h"
#include "hostlabel.h"

MainWindow::MainWindow()
	: QMainWindow()
{
	initActions();
	createMenus();
	createToolbars();

	QStatusBar *statusbar = new QStatusBar(this);
	setStatusBar(statusbar);

	hostaddress_ = new HostLabel();
	statusbar->addPermanentWidget(hostaddress_);

	netstatus_ = new NetStatus(this);
	statusbar->addPermanentWidget(netstatus_);

	readSettings();
}

void MainWindow::readSettings()
{
	QSettings cfg;
	cfg.beginGroup("mainwindow");

	resize(cfg.value("size",QSize(800,600)).toSize());

	if(cfg.contains("pos"))
		move(cfg.value("pos").toPoint());

	if(cfg.contains("state")) {
		if(restoreState(cfg.value("state").toByteArray())) {
			// Correct action tickmarks
			QToolBar *filebar = findChild<QToolBar*>("filetools");
			if(filebar && filebar->isHidden())
				toggleFileBar->setChecked(false);
			QToolBar *drawbar = findChild<QToolBar*>("drawtools");
			if(drawbar && drawbar->isHidden())
				toggleDrawBar->setChecked(false);
		}
	}
}

void MainWindow::writeSettings()
{
	QSettings cfg;
	cfg.beginGroup("mainwindow");

	cfg.setValue("pos", pos());
	cfg.setValue("size", size());
	cfg.setValue("state", saveState());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	(void)event;
	writeSettings();
}

void MainWindow::initActions()
{
	// File actions
	save_ = new QAction(QIcon(":icons/document-save.png"),tr("&Save"), this);
	save_->setShortcut(QKeySequence::Save);
	saveas_ = new QAction(QIcon(":icons/document-save-as.png"),tr("Save &As..."), this);
	quit_ = new QAction(QIcon(":icons/system-log-out.png"),tr("&Quit"), this);
	quit_->setShortcut(QKeySequence("Ctrl+Q"));
	quit_->setMenuRole(QAction::QuitRole);

	connect(quit_,SIGNAL(triggered()), this, SLOT(close()));

	// Session actions
	host_ = new QAction("Host...", this);
	join_ = new QAction("Join...", this);
	logout_ = new QAction("Leave", this);
	lockboard_ = new QAction("Lock the board", this);
	kickuser_ = new QAction("Kick", this);
	lockuser_ = new QAction("Lock", this);

	adminTools_ = new QActionGroup(this);
	adminTools_->addAction(lockboard_);
	adminTools_->addAction(kickuser_);
	adminTools_->addAction(lockuser_);

	// Drawing tool actions
	brushTool_ = new QAction(QIcon(":icons/draw-brush.png"),tr("Brush"), this);
	brushTool_->setCheckable(true); brushTool_->setChecked(true);
	eraserTool_ = new QAction(QIcon(":icons/draw-eraser.png"),tr("Eraser"), this);
	eraserTool_->setCheckable(true);
	zoomin_ = new QAction(QIcon(":icons/zoom-in.png"),tr("Zoom in"), this);
	zoomin_->setShortcut(QKeySequence::ZoomIn);
	zoomout_ = new QAction(QIcon(":icons/zoom-out.png"),tr("Zoom out"), this);
	zoomout_->setShortcut(QKeySequence::ZoomOut);

	drawingTools_ = new QActionGroup(this);
	drawingTools_->setExclusive(true);
	drawingTools_->addAction(brushTool_);
	drawingTools_->addAction(eraserTool_);

	// Toolbar toggling actions
	toggleFileBar = new QAction(tr("File"), this);
	toggleFileBar->setCheckable(true);
	toggleFileBar->setChecked(true);
	toggleDrawBar = new QAction(tr("Drawing tools"), this);
	toggleDrawBar->setCheckable(true);
	toggleDrawBar->setChecked(true);

	// Help actions
	help_ = new QAction(tr("DrawPile Help"), this);
	help_->setShortcut(QKeySequence("F1"));
	about_ = new QAction(tr("About DrawPile"), this);
	about_->setMenuRole(QAction::AboutRole);
}

void MainWindow::createMenus()
{
	QMenu *filemenu = menuBar()->addMenu(tr("&File"));
	filemenu->addAction(save_);
	filemenu->addAction(saveas_);
	filemenu->addSeparator();
	filemenu->addAction(quit_);

	QMenu *sessionmenu = menuBar()->addMenu(tr("&Session"));
	sessionmenu->addAction(host_);
	sessionmenu->addAction(join_);
	sessionmenu->addAction(logout_);
	sessionmenu->addSeparator();
	sessionmenu->addAction(lockboard_);
	sessionmenu->addAction(lockuser_);
	sessionmenu->addAction(kickuser_);

	QMenu *settingsmenu = menuBar()->addMenu(tr("Settings"));

	QMenu *windowmenu = menuBar()->addMenu(tr("&Window"));
	QMenu *toolbarmenu = windowmenu->addMenu(tr("Toolbars"));
	toolbarmenu->addAction(toggleFileBar);
	toolbarmenu->addAction(toggleDrawBar);
	windowmenu->addSeparator();
	windowmenu->addAction(zoomin_);
	windowmenu->addAction(zoomout_);

	QMenu *helpmenu = menuBar()->addMenu(tr("&Help"));
	helpmenu->addAction(help_);
	helpmenu->addSeparator();
	helpmenu->addAction(about_);
}

void MainWindow::createToolbars()
{
	QToolBar *filetools = new QToolBar(toggleFileBar->text());
	filetools->setObjectName("filetools");
	connect(toggleFileBar,SIGNAL(triggered(bool)),filetools,SLOT(setVisible(bool)));
	filetools->addAction(save_);
	filetools->addAction(saveas_);
	addToolBar(Qt::TopToolBarArea, filetools);

	QToolBar *drawtools = new QToolBar(toggleDrawBar->text());
	drawtools->setObjectName("drawtools");
	connect(toggleDrawBar,SIGNAL(triggered(bool)),drawtools,SLOT(setVisible(bool)));

	drawtools->addAction(brushTool_);
	drawtools->addAction(eraserTool_);
	drawtools->addSeparator();
	drawtools->addAction(zoomin_);
	drawtools->addAction(zoomout_);
	addToolBar(Qt::LeftToolBarArea, drawtools);

}

