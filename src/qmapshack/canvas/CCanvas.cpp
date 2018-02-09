/**********************************************************************************************
    Copyright (C) 2014 Oliver Eichler oliver.eichler@gmx.de
    Copyright (C) 2017 Norbert Truchsess norbert.truchsess@t-online.de

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

**********************************************************************************************/

#include "CMainWindow.h"
#include "GeoMath.h"
#include "canvas/CCanvas.h"
#include "canvas/CCanvasSetup.h"
#include "dem/CDemDraw.h"
#include "gis/CGisDraw.h"
#include "gis/CGisWorkspace.h"
#include "gis/IGisLine.h"
#include "gis/Poi.h"
#include "gis/ovl/CGisItemOvlArea.h"
#include "gis/trk/CGisItemTrk.h"
#include "grid/CGrid.h"
#include "grid/CGridSetup.h"
#include "helpers/CDraw.h"
#include "helpers/CSettings.h"
#include "map/CMapDraw.h"
#include "mouse/CMouseAdapter.h"
#include "mouse/CMouseEditArea.h"
#include "mouse/CMouseEditRte.h"
#include "mouse/CMouseEditTrk.h"
#include "mouse/CMouseMoveWpt.h"
#include "mouse/CMouseNormal.h"
#include "mouse/CMousePrint.h"
#include "mouse/CMouseRadiusWpt.h"
#include "mouse/CMouseRangeTrk.h"
#include "mouse/CMouseSelect.h"
#include "mouse/CMouseWptBubble.h"
#include "plot/CPlotProfile.h"
#include "realtime/CRtDraw.h"
#include "units/IUnit.h"
#include "widgets/CColorLegend.h"

#include <QtWidgets>

qreal CCanvas::gisLayerOpacity = 1.0;

CCanvas::CCanvas(QWidget *parent, const QString &name)
    : QWidget(parent)
{
    setFocusPolicy(Qt::WheelFocus);

    if(name.isEmpty())
    {
        for(int count = 1;; ++count)
        {
            QString name = tr("View %1").arg(count);
            if(nullptr == CMainWindow::self().findChild<CCanvas*>(name))
            {
                setObjectName(name);
                break;
            }
        }
    }
    else
    {
        setObjectName(name);
    }

    setMouseTracking(true);

    grabGesture(Qt::PinchGesture);

    map     = new CMapDraw(this);
    grid    = new CGrid(map);
    dem     = new CDemDraw(this);
    gis     = new CGisDraw(this);
    rt      = new CRtDraw(this);

    // map has to be first!
    allDrawContext << map << dem << gis << rt;

    mouse = new CMouseAdapter(this);
    mouse->setDelegate(new CMouseNormal(gis, this, mouse));

    connect(map, &CMapDraw::sigCanvasUpdate, this, &CCanvas::slotTriggerCompleteUpdate);
    connect(dem, &CDemDraw::sigCanvasUpdate, this, &CCanvas::slotTriggerCompleteUpdate);
    connect(gis, &CGisDraw::sigCanvasUpdate, this, &CCanvas::slotTriggerCompleteUpdate);
    connect(rt,  &CRtDraw::sigCanvasUpdate, this, &CCanvas::slotTriggerCompleteUpdate);

    timerToolTip = new QTimer(this);
    timerToolTip->setSingleShot(true);
    connect(timerToolTip, &QTimer::timeout, this, &CCanvas::slotToolTip);

    loadIndicator1 = new QMovie("://animation/loader.gif", QByteArray(), this);
    mapLoadIndicator = new QLabel(this);
    mapLoadIndicator->setMovie(loadIndicator1);
    loadIndicator1->start();
    mapLoadIndicator->show();

    loadIndicator2 = new QMovie("://animation/loader2.gif", QByteArray(), this);
    demLoadIndicator = new QLabel(this);
    demLoadIndicator->setMovie(loadIndicator2);
    loadIndicator2->start();
    demLoadIndicator->show();

    labelStatusMessages = new QLabel(this);
    labelStatusMessages->setWordWrap(true);
    labelStatusMessages->setMinimumWidth(300);
    labelStatusMessages->hide();

    labelTrackStatistic = new QLabel(this);
    labelTrackStatistic->setWordWrap(true);
    labelTrackStatistic->setMinimumWidth(300);
    labelTrackStatistic->hide();

    connect(map, &CMapDraw::sigStartThread, mapLoadIndicator, &QLabel::show);
    connect(map, &CMapDraw::sigStopThread,  mapLoadIndicator, &QLabel::hide);

    connect(dem, &CDemDraw::sigStartThread, demLoadIndicator, &QLabel::show);
    connect(dem, &CDemDraw::sigStopThread,  demLoadIndicator, &QLabel::hide);

    timerTrackOnFocus = new QTimer(this);
    timerTrackOnFocus->setSingleShot(false);
    timerTrackOnFocus->start(1000);

    connect(timerTrackOnFocus, &QTimer::timeout, this, &CCanvas::slotCheckTrackOnFocus);
}

CCanvas::~CCanvas()
{
    /* stop running drawing-threads and don't destroy unless they have finished*/
    for(IDrawContext * context : allDrawContext)
    {
        context->quit();
    }
    for(IDrawContext * context : allDrawContext)
    {
        context->wait();
    }

    /*
        Some mouse objects call methods from their canvas on destruction.
        So they are better deleted now explicitly before any other object
        in CCanvas is destroyed.
     */
    delete mouse;
    saveSizeTrackProfile();
}

void CCanvas::setOverrideCursor(const QCursor &cursor, const QString&)
{
//    qDebug() << "setOverrideCursor" << src;
    QApplication::setOverrideCursor(cursor);
}

void CCanvas::restoreOverrideCursor(const QString& src)
{
//    qDebug() << "restoreOverrideCursor" << src;
    QApplication::restoreOverrideCursor();
}

void CCanvas::changeOverrideCursor(const QCursor& cursor, const QString &src)
{
//    qDebug() << "changeOverrideCursor" << src;
    QApplication::changeOverrideCursor(cursor);
}

void CCanvas::triggerCompleteUpdate(CCanvas::redraw_e flags)
{
    CCanvas * canvas = CMainWindow::self().getVisibleCanvas();
    if(canvas)
    {
        canvas->slotTriggerCompleteUpdate(flags);
    }
}

void CCanvas::saveConfig(QSettings& cfg)
{
    map->saveConfig(cfg);
    dem->saveConfig(cfg);
    grid->saveConfig(cfg);
    cfg.setValue("posFocus",  posFocus);
    cfg.setValue("proj",      map->getProjection());
    cfg.setValue("scales",    map->getScalesType());
    cfg.setValue("backColor", backColor.name());
}

void CCanvas::loadConfig(QSettings& cfg)
{
    posFocus = cfg.value("posFocus", posFocus).toPointF();
    setProjection(cfg.value("proj", map->getProjection()).toString());
    setScales((CCanvas::scales_type_e)cfg.value("scales",  map->getScalesType()).toInt());

    const QString &backColorStr = cfg.value("backColor", "#FFFFBF").toString();
    backColor = QColor(backColorStr);

    map->loadConfig(cfg);
    dem->loadConfig(cfg);
    grid->loadConfig(cfg);

    for(IDrawContext * context : allDrawContext.mid(1))
    {
        context->zoom(map->zoom());
    }
}

void CCanvas::resetMouse()
{
    mouse->setDelegate(new CMouseNormal(gis, this, mouse));
    if(underMouse())
    {
        while(QApplication::overrideCursor())
        {
            CCanvas::restoreOverrideCursor("resetMouse");
        }
        CCanvas::setOverrideCursor(*mouse, "resetMouse");
    }
}

void CCanvas::mouseTrackingLost()
{
    mouseLost = true;
}

void CCanvas::setMouseMoveWpt(CGisItemWpt& wpt)
{
    mouse->setDelegate(new CMouseMoveWpt(wpt, gis, this, mouse));
}

void CCanvas::setMouseRadiusWpt(CGisItemWpt& wpt)
{
    mouse->setDelegate(new CMouseRadiusWpt(wpt, gis, this, mouse));
}

void CCanvas::setMouseEditTrk(const QPointF &pt)
{
    mouse->setDelegate(new CMouseEditTrk(pt, gis, this, mouse));
}

void CCanvas::setMouseEditRte(const QPointF &pt)
{
    mouse->setDelegate(new CMouseEditRte(pt, gis, this, mouse));
}

void CCanvas::setMouseEditTrk(CGisItemTrk& trk)
{
    mouse->setDelegate(new CMouseEditTrk(trk, gis, this, mouse));
}

void CCanvas::setMouseRangeTrk(CGisItemTrk& trk)
{
    mouse->setDelegate(new CMouseRangeTrk(trk, gis, this, mouse));
}

void CCanvas::setMouseEditArea(const QPointF& pt)
{
    mouse->setDelegate(new CMouseEditArea(pt, gis, this, mouse));
}

void CCanvas::setMouseEditArea(CGisItemOvlArea& area)
{
    mouse->setDelegate(new CMouseEditArea(area, gis, this, mouse));
}

void CCanvas::setMouseEditRte(CGisItemRte& rte)
{
    mouse->setDelegate(new CMouseEditRte(rte, gis, this, mouse));
}

void CCanvas::setMouseWptBubble(const IGisItem::key_t& key)
{
    mouse->setDelegate(new CMouseWptBubble(key, gis, this, mouse));
}

void CCanvas::setMousePrint()
{
    mouse->setDelegate(new CMousePrint(gis, this, mouse));
}

void CCanvas::setMouseSelect()
{
    mouse->setDelegate(new CMouseSelect(gis, this, mouse));
}

void CCanvas::reportStatus(const QString& key, const QString& msg)
{
    if(msg.isEmpty())
    {
        statusMessages.remove(key);
    }
    else
    {
        statusMessages[key] = msg;
    }

    QString report;
    QStringList keys = statusMessages.keys();
    keys.sort();
    for(const QString &key : keys)
    {
        report += statusMessages[key] + "\n";
    }

    if(report.isEmpty())
    {
        labelStatusMessages->hide();
    }
    else
    {
        labelStatusMessages->show();
        labelStatusMessages->setText(report);
        labelStatusMessages->adjustSize();
    }
    update();
}

void CCanvas::resizeEvent(QResizeEvent * e)
{
    needsRedraw = eRedrawAll;

    setDrawContextSize(e->size());
    QWidget::resizeEvent(e);

    const QRect& r = rect();

    // move map loading indicator to new center of canvas
    QPoint p1(mapLoadIndicator->width()>>1, mapLoadIndicator->height()>>1);
    mapLoadIndicator->move(r.center() - p1);

    QPoint p2(demLoadIndicator->width()>>1, demLoadIndicator->height()>>1);
    demLoadIndicator->move(r.center() - p2);

    labelStatusMessages->move(20,50);

    slotUpdateTrackStatistic(CMainWindow::self().isMinMaxTrackValues());
    setSizeTrackProfile();
}

void CCanvas::paintEvent(QPaintEvent*)
{
    if(!isVisible())
    {
        return;
    }

    QPainter p;
    p.begin(this);
    USE_ANTI_ALIASING(p,true);

    // fill the background with default pattern
    p.fillRect(rect(), backColor);

    // ----- start to draw thread based content -----
    // move coordinate system to center of the screen
    p.translate(width() >> 1, height() >> 1);

    map->draw(p, needsRedraw, posFocus);
    dem->draw(p, needsRedraw, posFocus);
    p.setOpacity(gisLayerOpacity);
    gis->draw(p, needsRedraw, posFocus);
    rt->draw(p, needsRedraw, posFocus);
    p.setOpacity(1.0);

    // restore coordinate system to default
    p.resetTransform();
    // ----- start to draw fast content -----

    grid->draw(p, rect());
    if(map->isFinished() && dem->isFinished())
    {
        if(gis->isFinished())
        {
            gis->draw(p, rect());
        }
        if(rt->isFinished())
        {
            rt->draw(p, rect());
        }
    }
    mouse->draw(p, needsRedraw, rect());


    drawStatusMessages(p);
    drawTrackStatistic(p);
    drawScale(p);

    p.end();
    needsRedraw = eRedrawNone;
}

void CCanvas::mousePressEvent(QMouseEvent * e)
{
    if(!mousePressMutex.tryLock())
    {
        return;
    }

    mouse->mousePressEvent(e);
    QWidget::mousePressEvent(e);
    e->accept();

    mousePressMutex.unlock();
}

void CCanvas::mouseMoveEvent(QMouseEvent * e)
{
    QPointF pos = e->pos();
    map->convertPx2Rad(pos);
    qreal ele = dem->getElevationAt(pos);
    qreal slope = dem->getSlopeAt(pos);
    emit sigMousePosition(pos * RAD_TO_DEG, ele, slope);

    mouse->mouseMoveEvent(e);
    QWidget::mouseMoveEvent(e);
    e->accept();
}

void CCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    mouse->mouseReleaseEvent(e);
    QWidget::mouseReleaseEvent(e);
    e->accept();
}

void CCanvas::mouseDoubleClickEvent(QMouseEvent * e)
{
    mouse->mouseDoubleClickEvent(e);
    QWidget::mouseDoubleClickEvent(e);
}

void CCanvas::wheelEvent(QWheelEvent * e)
{
    mouse->wheelEvent(e);

    // angleDelta() returns the eighths of a degree
    // of the mousewheel
    // -> zoom in/out every 15 degrees = every 120 eights
    const int EIGHTS_ZOOM = 15 * 8;
    zoomAngleDelta += e->angleDelta().y();
    if(abs(zoomAngleDelta) < EIGHTS_ZOOM)
    {
        return;
    }

    zoomAngleDelta = 0;

    QPointF pos = e->posF();
    QPointF pt1 = pos;

    map->convertPx2Rad(pt1);
    setZoom(CMainWindow::self().flipMouseWheel() ? (e->delta() < 0) : (e->delta() > 0), needsRedraw);
    map->convertRad2Px(pt1);

    map->convertRad2Px(posFocus);
    posFocus -= (pos - pt1);
    map->convertPx2Rad(posFocus);

    update();
}


void CCanvas::enterEvent(QEvent * e)
{
    Q_UNUSED(e);
    CCanvas::setOverrideCursor(*mouse, "enterEvent");

    setMouseTracking(true);
}


void CCanvas::leaveEvent(QEvent *)
{
    // bad hack to stop bad number of override cursors.
    while(QApplication::overrideCursor())
    {
        CCanvas::restoreOverrideCursor("leaveEvent");
    }

    setMouseTracking(false);
}

void CCanvas::keyPressEvent(QKeyEvent * e)
{
    qDebug() << hex << e->key();
    bool doUpdate = true;

    switch(e->key())
    {
    case Qt::Key_Plus:
        setZoom(true, needsRedraw);
        break;

    case Qt::Key_Minus:
        setZoom(false, needsRedraw);
        break;

    /* move the map with keys up, down, left and right */
    case Qt::Key_Up:
        moveMap(QPointF(0,  height()/4));
        break;

    case Qt::Key_Down:
        moveMap(QPointF(0, -height()/4));
        break;

    case Qt::Key_Left:
        moveMap(QPointF( width()/4, 0));
        break;

    case Qt::Key_Right:
        moveMap(QPointF(-width()/4, 0));
        break;

    case Qt::Key_Escape:
    {
        break;
    }

    default:
        doUpdate = false;
    }

    if(doUpdate)
    {
        mouse->keyPressEvent(e);
        e->accept();
        update();
    }
    else
    {
        QWidget::keyPressEvent(e);
    }
}

void CCanvas::drawStatusMessages(QPainter& p)
{
    if(labelStatusMessages->isVisible())
    {
        QRect r = labelStatusMessages->frameGeometry();
        r.adjust(-5, -5, 5, 5);
        p.setPen(CDraw::penBorderGray);
        p.setBrush(CDraw::brushBackWhite);
        p.drawRoundedRect(r, RECT_RADIUS, RECT_RADIUS);
    }
}

void CCanvas::drawTrackStatistic(QPainter& p)
{
    if(labelTrackStatistic->isVisible())
    {
        QRect r = labelTrackStatistic->frameGeometry();
        r.adjust(-5, -5, 5, 5);
        p.setPen(CDraw::penBorderGray);
        p.setBrush(CDraw::brushBackWhite);
        p.drawRoundedRect(r, RECT_RADIUS, RECT_RADIUS);
    }
}

void CCanvas::drawScale(QPainter& p)
{
    if(!CMainWindow::self().isScaleVisible())
    {
        return;
    }

    // step I: get the approximate distance for 200px in the bottom right corner
    QPointF brc(rect().bottomRight() - QPoint(50,30));
    QPointF pt1 = brc;
    QPointF pt2 = brc - QPoint(-200,0);

    map->convertPx2Rad(pt1);
    map->convertPx2Rad(pt2);

    qreal d = GPS_Math_Distance(pt1.x(), pt1.y(), pt2.x(), pt2.y());

    // step II: derive the actual scale length in [m]
    qreal a = (int)log10(d);
    qreal b = log10(d) - a;

    if(0 <= b && b < log10(3.0f))
    {
        d = 1 * qPow(10,a);
    }
    else if(log10(3.0f) < b && b < log10(5.0f))
    {
        d = 3 * qPow(10,a);
    }
    else
    {
        d = 5 * qPow(10,a);
    }

    // step III: convert the scale length from [m] into [px]
    pt1 = brc;
    map->convertPx2Rad(pt1);
    pt2 = GPS_Math_Wpt_Projection(pt1, d, -90 * DEG_TO_RAD);

    map->convertRad2Px(pt1);
    map->convertRad2Px(pt2);

    p.setPen(QPen(Qt::white, 9));
    p.drawLine(pt1, pt2 + QPoint(9,0));
    p.setPen(QPen(Qt::black, 7));
    p.drawLine(pt1, pt2 + QPoint(9,0));
    p.setPen(QPen(Qt::white, 5));
    p.drawLine(pt1, pt2 + QPoint(9,0));

    QVector<qreal> pattern;
    pattern << 2 << 4;
    QPen pen(Qt::black, 5, Qt::CustomDashLine);
    pen.setDashPattern(pattern);
    p.setPen(pen);
    p.drawLine(pt1, pt2 + QPoint(9,0));


    QPoint pt3(pt2.x() + (pt1.x() - pt2.x())/2, pt2.y());

    QString val, unit;
    IUnit::self().meter2distance(d,val,unit);
    CDraw::text(QString("%1 %2").arg(val).arg(unit), p, pt3, Qt::black);
}

void CCanvas::slotTriggerCompleteUpdate(CCanvas::redraw_e flags)
{
    needsRedraw = (redraw_e)(needsRedraw | flags);
    update();
}


void CCanvas::slotToolTip()
{
    QString str;
    map->getToolTip(posToolTip, str);
    if(str.isEmpty())
    {
        return;
    }
    QPoint p = (posToolTip + QPoint(32,0));
    QToolTip::showText(p,str);
}

void CCanvas::slotCheckTrackOnFocus()
{
    const IGisItem::key_t& key = CGisItemTrk::getKeyUserFocus();

    // any changes?
    if(key != keyTrackOnFocus)
    {
        saveSizeTrackProfile();
        // get access to current track object
        delete plotTrackProfile;
        delete colorLegend;
        keyTrackOnFocus.clear();
        labelTrackStatistic->clear();
        labelTrackStatistic->hide();

        // get access to next track object
        CGisItemTrk * trk2 = dynamic_cast<CGisItemTrk*>(CGisWorkspace::self().getItemByKey(key));
        if(nullptr == trk2)
        {
            return;
        }

        // create new profile plot, the plot will register itself at the track
        plotTrackProfile = new CPlotProfile(trk2, trk2->limitsGraph1, CMainWindow::self().profileIsWindow() ? IPlot::eModeWindow : IPlot::eModeIcon, this);
        setSizeTrackProfile();
        if(isVisible())
        {
            plotTrackProfile->show();
        }

        colorLegend = new CColorLegend(this, trk2);
        colorLegend->setGeometry(20, 20, 40, 300);

        // finally store the new key as track on focus
        keyTrackOnFocus = key;

        slotUpdateTrackStatistic(CMainWindow::self().isMinMaxTrackValues());
    }
}

void CCanvas::slotUpdateTrackStatistic(bool show)
{
    CGisItemTrk * trk = dynamic_cast<CGisItemTrk*>(CGisWorkspace::self().getItemByKey(keyTrackOnFocus));

    if(show && trk)
    {
        QString text = trk->getInfo(IGisItem::eFeatureShowName|IGisItem::eFeatureShowActivity);
        text += trk->getInfoLimits();

        labelTrackStatistic->setMinimumWidth((trk->getActivities().getActivityCount() > 1) ? 450 : 350);
        labelTrackStatistic->setText(text);
        labelTrackStatistic->adjustSize();

        labelTrackStatistic->move(rect().width() - labelTrackStatistic->width() - 20, rect().height() - labelTrackStatistic->height() - 60);
        labelTrackStatistic->show();
        update();
    }
    else
    {
        labelTrackStatistic->clear();
        labelTrackStatistic->hide();
    }
}

void CCanvas::moveMap(const QPointF& delta)
{
    map->convertRad2Px(posFocus);
    posFocus -= delta;
    map->convertPx2Rad(posFocus);

    emit sigMove();

    slotTriggerCompleteUpdate(eRedrawAll);
}

void CCanvas::zoomTo(const QRectF& rect)
{
    posFocus = rect.center();
    map->zoom(rect);
    for(IDrawContext * context : allDrawContext.mid(1))
    {
        context->zoom(map->zoom());
    }

    slotTriggerCompleteUpdate(eRedrawAll);
}

void CCanvas::setupGrid()
{
    CGridSetup dlg(grid, map);
    dlg.exec();
    update();
}

void CCanvas::setupBackgroundColor()
{
    QColorDialog::setCustomColor(0, "#FFFFBF");
    const QColor &selected = QColorDialog::getColor(backColor, this, tr("Setup Map Background"));

    if(selected.isValid())
    {
        backColor = selected;
        update();
    }
}

void CCanvas::convertGridPos2Str(const QPointF& pos, QString& str, bool simple)
{
    grid->convertPos2Str(pos, str, simple);
}

void CCanvas::convertRad2Px(QPointF& pos) const
{
    map->convertRad2Px(pos);
}

void CCanvas::convertPx2Rad(QPointF& pos) const
{
    map->convertPx2Rad(pos);
}

void CCanvas::displayInfo(const QPoint& px)
{
    if(CMainWindow::self().isMapToolTip())
    {
        posToolTip = px;

        timerToolTip->stop();
        timerToolTip->start(500);
    }
    QToolTip::hideText();
}

poi_t CCanvas::findPOICloseBy(const QPoint& px) const
{
    return map->findPOICloseBy(px);
}

void CCanvas::setup()
{
    CCanvasSetup dlg(this);
    dlg.exec();
}

QString CCanvas::getProjection()
{
    return map->getProjection();
}

void CCanvas::setProjection(const QString& proj)
{
    for(IDrawContext * context : allDrawContext)
    {
        context->setProjection(proj);
    }
}

void CCanvas::setScales(const scales_type_e type)
{
    for(IDrawContext * context : allDrawContext)
    {
        context->setScales(type);
    }
}

CCanvas::scales_type_e CCanvas::getScalesType()
{
    return map->getScalesType();
}


qreal CCanvas::getElevationAt(const QPointF& pos) const
{
    return dem->getElevationAt(pos);
}

void CCanvas::getElevationAt(const QPolygonF& pos, QPolygonF& ele) const
{
    return dem->getElevationAt(pos, ele);
}

qreal CCanvas::getSlopeAt(const QPointF& pos) const
{
    return dem->getSlopeAt(pos);
}

void CCanvas::getSlopeAt(const QPolygonF& pos, QPolygonF& slope) const
{
    return dem->getSlopeAt(pos, slope);
}

void CCanvas::getElevationAt(SGisLine& line) const
{
    return dem->getElevationAt(line);
}

void CCanvas::setZoom(bool in, redraw_e& needsRedraw)
{
    map->zoom(in, needsRedraw);

    for(IDrawContext * context : allDrawContext.mid(1))
    {
        context->zoom(map->zoom());
    }

    emit sigZoom();
}

bool CCanvas::findPolylineCloseBy(const QPointF& pt1, const QPointF& pt2, qint32 threshold, QPolygonF& polyline)
{
    return map->findPolylineCloseBy(pt1, pt2, threshold, polyline);
}

void CCanvas::saveSizeTrackProfile()
{
    if(plotTrackProfile.isNull())
    {
        return;
    }

    if(plotTrackProfile->windowFlags() & Qt::Window)
    {
        SETTINGS;
        cfg.beginGroup("Canvas");
        cfg.beginGroup("Profile");
        cfg.beginGroup(objectName());

        cfg.setValue("geometry", plotTrackProfile->saveGeometry());

        cfg.endGroup(); // objectName()
        cfg.endGroup(); // Profile
        cfg.endGroup(); // Canvas
    }
}

void CCanvas::setSizeTrackProfile()
{
    if(plotTrackProfile.isNull())
    {
        return;
    }

    if(plotTrackProfile->windowFlags() & Qt::Window)
    {
        SETTINGS;
        cfg.beginGroup("Canvas");
        cfg.beginGroup("Profile");
        cfg.beginGroup(objectName());

        if(cfg.contains("geometry"))
        {
            plotTrackProfile->restoreGeometry(cfg.value("geometry").toByteArray());
        }
        else
        {
            plotTrackProfile->resize(300,200);
            plotTrackProfile->move(100,100);
        }

        cfg.endGroup(); // objectName()
        cfg.endGroup(); // Profile
        cfg.endGroup(); // Canvas
    }
    else
    {
        if(size().height() < 700)
        {
            plotTrackProfile->resize(200,80);
        }
        else
        {
            plotTrackProfile->resize(300,120);
        }

        plotTrackProfile->move(20, height() - plotTrackProfile->height() - 20);
    }
}

void CCanvas::showProfileAsWindow(bool yes)
{
    if(plotTrackProfile)
    {
        const IGisItem::key_t key = CGisItemTrk::getKeyUserFocus();

        delete plotTrackProfile;
        keyTrackOnFocus.clear();

        CGisWorkspace::self().focusTrkByKey(true, key);
    }
}

void CCanvas::showProfile(bool yes)
{
    if(nullptr != plotTrackProfile)
    {
        plotTrackProfile->setVisible(yes);
    }
}

void CCanvas::setDrawContextSize(const QSize& s)
{
    for(IDrawContext * context : allDrawContext)
    {
        context->resize(s);
    }
}

void CCanvas::print(QPainter& p, const QRectF& area, const QPointF& focus)
{
    const QSize oldSize = size();
    const QSize newSize(area.size().toSize());

    setDrawContextSize(newSize);

    // ----- start to draw thread based content -----
    // move coordinate system to center of the screen
    p.translate(newSize.width() >> 1, newSize.height() >> 1);

    redraw_e redraw = eRedrawAll;

    for(IDrawContext * context : allDrawContext)
    {
        context->draw(p, redraw, focus);
    }

    for(IDrawContext * context : allDrawContext)
    {
        context->wait();
    }

    for(IDrawContext * context : allDrawContext)
    {
        context->draw(p, redraw, focus);
    }

    // restore coordinate system to default
    p.resetTransform();
    // ----- start to draw fast content -----

    QRect r(QPoint(0,0), area.size().toSize());

    grid->draw(p, r);
    gis->draw(p, r);
    rt->draw(p, r);

    setDrawContextSize(oldSize);
}

bool CCanvas::event(QEvent *event)
{
    if (event->type() == QEvent::Gesture)
    {
        return gestureEvent(static_cast<QGestureEvent*>(event));
    }
    else if (mouseLost)
    {
        QMouseEvent * me = dynamic_cast<QMouseEvent*>(event);
        if (me != nullptr)
        {
            // notify IMouse that the upcomming QMouseEvent needs special treatment
            // as some mouse-events may have been lost
            mouse->afterMouseLostEvent(me);
            mouseLost = false;
        }
    }
    return QWidget::event(event);
}

bool CCanvas::gestureEvent(QGestureEvent* e)
{
    if (QPinchGesture *pinch = dynamic_cast<QPinchGesture *>(e->gesture(Qt::PinchGesture)))
    {
        if (pinch->changeFlags() & QPinchGesture::CenterPointChanged)
        {
            const QPointF & move = pinch->centerPoint() - pinch->lastCenterPoint();
            if (!move.isNull())
            {
                moveMap(move);
            }
        }
        if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged)
        {
            qreal pscale = pinch->totalScaleFactor();
            if (pscale < 0.8f || pscale > 1.25f)
            {
                const QPointF & center = pinch->centerPoint();
                const QPointF & pos = mapFromGlobal(QPoint(center.x(),center.y()));
                QPointF pt1 = pos;
                map->convertPx2Rad(pt1);
                setZoom(pscale > 1.0f, needsRedraw);
                map->convertRad2Px(pt1);
                const QPointF & move = pos - pt1;
                if (!move.isNull())
                {
                    moveMap(move);
                }
                pinch->setTotalScaleFactor(1.0f);
                slotTriggerCompleteUpdate(needsRedraw);
            }
        }
        mouseLost = true;
        mouse->pinchGestureEvent(pinch);
    }
    return true;
}