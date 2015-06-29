/**********************************************************************************************
    Copyright (C) 2014-2015 Oliver Eichler oliver.eichler@gmx.de

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

#include "helpers/CSettings.h"
#include "tool/CRoutinoDatabaseBuilder.h"

#include <QtWidgets>

CRoutinoDatabaseBuilder::CRoutinoDatabaseBuilder(QWidget * parent)
    : IToolShell(textBrowser, parent)
    , first(true)
    , tainted(false)
    , last(false)
{
    setupUi(this);

    setObjectName(tr("Create Routino Database"));

    connect(toolSourceFiles, SIGNAL(clicked()), this, SLOT(slotSelectSourceFiles()));
    connect(toolTargetPath, SIGNAL(clicked()), this, SLOT(slotSelectTargetPath()));
    connect(pushStart, SIGNAL(clicked()), this, SLOT(slotStart()));
    connect(lineTargetPrefix, SIGNAL(editingFinished()), this, SLOT(enabelStartButton()));

    pushStart->setDisabled(true);

    QFile _translations("://xml/routino/routino-tagging.xml");
    _translations.open(QIODevice::ReadOnly);

    xmlTagging.open();
    xmlTagging.write(_translations.readAll());
    xmlTagging.close();

    SETTINGS;
    QString path = cfg.value("RoutinoDatabaseBuilder/targetPath",QDir::homePath()).toString();

    labelTargetPath->setText(path);
}

CRoutinoDatabaseBuilder::~CRoutinoDatabaseBuilder()
{
}

void CRoutinoDatabaseBuilder::slotSelectSourceFiles()
{
    SETTINGS;
    QString path = cfg.value("RoutinoDatabaseBuilder/sourcePath",QDir::homePath()).toString();

    QStringList files = QFileDialog::getOpenFileNames(this, tr("Select files..."), path, "OSM Database (*.pbf)");
    if(files.isEmpty())
    {
        return;
    }

    QFileInfo fi(files.first());
    path = fi.absolutePath();
    cfg.setValue("RoutinoDatabaseBuilder/sourcePath", path);

    listWidget->clear();
    foreach(const QString &file, files)
    {
        new QListWidgetItem(QIcon("://icons/32x32/Map.png"), file, listWidget);
    }

    enabelStartButton();
}

void CRoutinoDatabaseBuilder::slotSelectTargetPath()
{
    SETTINGS;
    QString path = cfg.value("RoutinoDatabaseBuilder/targetPath",QDir::homePath()).toString();

    path = QFileDialog::getExistingDirectory(this, tr("Select target path..."), path);
    if(path.isEmpty())
    {
        return;
    }

    cfg.setValue("RoutinoDatabaseBuilder/targetPath", path);


    labelTargetPath->setText(path);

    enabelStartButton();
}


void CRoutinoDatabaseBuilder::enabelStartButton()
{
    pushStart->setDisabled(true);
    if(listWidget->count() == 0)
    {
        return;
    }
    if(labelTargetPath->text() == "-")
    {
        return;
    }

    if(lineTargetPrefix->text().isEmpty())
    {
        return;
    }

    pushStart->setEnabled(true);
}


void CRoutinoDatabaseBuilder::slotStart()
{

    pushStart->setDisabled(true);

    sourceFiles.clear();
    foreach(const QListWidgetItem * item, listWidget->findItems("*", Qt::MatchWildcard))
    {
        sourceFiles << item->text();
    }

    targetPrefix    = lineTargetPrefix->text();
    targetPath      = labelTargetPath->text();
    first           = true;
    last            = false;

    textBrowser->clear();

    slotFinished(0,QProcess::NormalExit);
}

void CRoutinoDatabaseBuilder::finished(int exitCode, QProcess::ExitStatus status)
{
    if(last)
    {
        textBrowser->setTextColor(Qt::darkGreen);
        textBrowser->append(tr("!!! done !!!\n"));
        pushStart->setEnabled(true);
        return;
    }

    if(sourceFiles.isEmpty())
    {
        QStringList args;

        args << QString("--dir=%1").arg(targetPath);
        args << QString("--prefix=%1").arg(targetPrefix);
        args << QString("--tagging=%1").arg(xmlTagging.fileName());
        args << "--process-only";

        stdOut("planetsplitter " +  args.join(" ") + "\n");
        cmd.start("planetsplitter", args);

        last = true;
    }
    else
    {
        QStringList args;

        args << QString("--dir=%1").arg(targetPath);
        args << QString("--prefix=%1").arg(targetPrefix);
        args << QString("--tagging=%1").arg(xmlTagging.fileName());

        if(first)
        {
            first = false;
            args << "--parse-only";
        }
        else
        {
            args << "--parse-only" << "--append";
        }

        args << sourceFiles.first();
        sourceFiles.pop_front();


        stdOut("planetsplitter " +  args.join(" ") + "\n");
        cmd.start("planetsplitter", args);
    }
}