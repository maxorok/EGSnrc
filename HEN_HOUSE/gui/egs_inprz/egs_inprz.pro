
###############################################################################
#
#  EGSnrc Qt project file for the egs_inprz graphical user interface
#  Copyright (C) 2015 National Research Council Canada
#
#  This file is part of EGSnrc.
#
#  EGSnrc is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Affero General Public License as published by the
#  Free Software Foundation, either version 3 of the License, or (at your
#  option) any later version.
#
#  EGSnrc is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
#  more details.
#
#  You should have received a copy of the GNU Affero General Public License
#  along with EGSnrc. If not, see <http://www.gnu.org/licenses/>.
#
###############################################################################
#
#  Author:          Ernesto Mainegra-Hing, 2003
#
#  Contributors:    Iwan Kawrakow
#                   Blake Walters
#
###############################################################################


SOURCES	+= src/cavinputs.cpp \
	src/commandManager.cpp \
	src/de_parser.cpp \
	src/errordlg.cpp \
	src/eventfilter.cpp \
	src/executiondlgImpl.cpp \
	src/geoinputs.cpp \
	src/inputblock.cpp \
	src/beamsrcdlg.cpp \
	src/inputRZForm.cpp \
	src/inputRZImpl.cpp \
	src/inputRZPrint.cpp \
	src/inputRZSave.cpp \
	src/inputRZTools.cpp \
	src/inputs.cpp \
	src/ioinputs.cpp \
	src/mainRZ.cpp \
	src/mcinputs.cpp \
	src/mcpinputs.cpp \
	src/phdinputs.cpp \
        src/pegslessinputs.cpp \
	src/plotinputs.cpp \
	src/richtext.cpp \
	src/srcinputs.cpp \
	src/title.cpp \
	src/varinputs.cpp \
	src/tools.cpp \
        src/egs_config_reader.cpp
HEADERS	+= include/cavinputs.h \
	include/commandManager.h \
	include/datainp.h \
	include/de_parser.h \
	include/errordlg.h \
	include/eventfilter.h \
	include/executiondlgImpl.h \
	include/geoinputs.h \
	include/inputblock.h \
	include/beamsrcdlg.h \
	include/inputRZImpl.h \
	include/inputs.h \
	include/ioinputs.h \
	include/mcinputs.h \
	include/mcpinputs.h \
	include/phdinputs.h \
        include/pegslessinputs.h \
	include/plotinputs.h \
	include/queuedef.h \
	include/richtext.h \
	include/srcinputs.h \
	include/srctooltip.h \
	include/title.h \
	include/tooltips.h \
	include/varinputs.h \
	include/tools.h \
        ../egs_gui/egs_config_reader.h

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport


######################################################################
# Automatically generated by qmake (1.03a) Thu Dec 5 10:07:20 2002
######################################################################

DEPENDPATH += src include ui ../egs_gui

# Input

# MOC_DIR = .moc/
# OBJECTS_DIR = .obj/

MOC_DIR =       .moc/$$my_machine
OBJECTS_DIR =   .obj/$$my_machine
DESTDIR  = ../../bin/$$my_machine


UI_HEADERS_DIR = include/
UI_SOURCES_DIR = src/

win32 {
      DEFINES += WIN32
      CONFIG  += qt thread warn_off release stl windows
      RC_FILE = egs_inprz.rc
}

unix {
    CONFIG += qt thread warn_off release stl $$my_build
    contains( CONFIG, shared ):message( "Dynamic build..." )
    contains( CONFIG, static ){
        message( "Static build ..." )
        DESTDIR = ../../pieces/linux
        UNAME = $$system(getconf LONG_BIT)
        contains( UNAME, 64 ){
           message( "-> 64 bit ($$SNAME)" )
           TARGET = egs_inprz_64
        }
        contains( UNAME, 32 ){
           message( "-> 32 bit ($$SNAME)" )
           TARGET = egs_inprz_32
        }
        QMAKE_POST_LINK = strip $(TARGET)
    }
}
FORMS	= ui/inputRZ.ui \
	ui/executiondialog.ui
TEMPLATE	=app
INCLUDEPATH	+= include ui ../egs_gui
LANGUAGE	= C++

