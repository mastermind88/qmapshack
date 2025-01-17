#!/bin/sh

DIR_SCRIPT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"  # absolute path to the dir of this script
source $DIR_SCRIPT/config.sh   # check for important paramters


echo "${INFO}Bundling QMapShack.app${NC}"


set -a
APP_NAME=QMapShack
set +a

source $DIR_SCRIPT/bundle-env-path.sh
source $DIR_SCRIPT/bundle-common-func.sh


function extendAppStructure {
    mkdir $BUILD_BUNDLE_RES_QM_DIR
    mkdir $BUILD_BUNDLE_RES_GDAL_DIR
    mkdir $BUILD_BUNDLE_RES_PROJ_DIR
    mkdir $BUILD_BUNDLE_RES_ROUTINO_DIR
    mkdir $BUILD_BUNDLE_RES_HELP_DIR
    mkdir $BUILD_BUNDLE_RES_BIN_DIR
}


function copyAdditionalLibraries {
    cp -v    $LOCAL_ENV/lib/libroutino* $BUILD_BUNDLE_FRW_DIR
    cp -v    $LOCAL_ENV/lib/libquazip*.dylib $BUILD_BUNDLE_FRW_DIR

    if [ -z "$BREW_PACKAGE_BUILD"]; then
        # copy only if built as standalone package (QMS not as a brew pkg)
        cp -v    $GDAL_DIR/lib/libgdal*.dylib $BUILD_BUNDLE_FRW_DIR
        cp -v    $HOMEBREW_PREFIX/lib/libgeos*.dylib $BUILD_BUNDLE_EXTLIB_DIR

        cp -v    $HOMEBREW_PREFIX/lib/libproj*.dylib $BUILD_BUNDLE_FRW_DIR

        cp -v    $HOMEBREW_PREFIX/lib/libdbus*.dylib $BUILD_BUNDLE_FRW_DIR

        cp -v -R $QT_DIR/lib/QtOpenGL.framework $BUILD_BUNDLE_FRW_DIR
        cp -v -R $QT_DIR/lib/QtQuick.framework $BUILD_BUNDLE_FRW_DIR
        cp -v -R $QT_DIR/lib/QtQml.framework $BUILD_BUNDLE_FRW_DIR
    fi

    # remove debug libraries
    for F in `find $BUILD_BUNDLE_FRW_DIR/Qt*.framework/* -type f -name '*_debug*'`
    do
        echo $F
        rm $F
    done
    
    # remove static libraries
    rm -f $BUILD_BUNDLE_FRW_DIR/lib*.a
}


function copyExternalFiles {
    cp -v $GDAL_DIR/share/gdal/* $BUILD_BUNDLE_RES_GDAL_DIR

    cp -v $HOMEBREW_PREFIX/share/proj/* $BUILD_BUNDLE_RES_PROJ_DIR
    rm $BUILD_BUNDLE_RES_PROJ_DIR/*.tif
    rm $BUILD_BUNDLE_RES_PROJ_DIR/*.txt

    cp -v $LOCAL_ENV/xml/profiles.xml $BUILD_BUNDLE_RES_ROUTINO_DIR
    cp -v $LOCAL_ENV/xml/translations.xml $BUILD_BUNDLE_RES_ROUTINO_DIR
    cp -v $LOCAL_ENV/xml/tagging.xml $BUILD_BUNDLE_RES_ROUTINO_DIR    
}

function copyExternalHelpFiles_QMS {
    cp -v $HELP_QMS_DIR/QMSHelp.qch $BUILD_BUNDLE_RES_HELP_DIR
    cp -v $HELP_QMS_DIR/QMSHelp.qhc $BUILD_BUNDLE_RES_HELP_DIR
}


function copyExtTools {
    if [ -z "$BREW_PACKAGE_BUILD"]; then
        # copy only if built as standalone package (QMS not as a brew pkg)
        cp -v $GDAL_DIR/bin/gdalbuildvrt            $BUILD_BUNDLE_RES_BIN_DIR
        cp -v $HOMEBREW_PREFIX/bin/proj             $BUILD_BUNDLE_RES_BIN_DIR
    fi
    cp -v $LOCAL_ENV/lib/planetsplitter         $BUILD_BUNDLE_RES_BIN_DIR
    # currently only used by QMapTool.
    cp -v $BUILD_BIN_DIR/qmt_rgb2pct            $BUILD_BUNDLE_RES_BIN_DIR
    cp -v $BUILD_BIN_DIR/qmt_map2jnx            $BUILD_BUNDLE_RES_BIN_DIR
}


function archiveBundle {
    ARCHIVE=$(printf "%s/%s-MacOSX_%s.tar.gz" "$BUILD_RELEASE_DIR" "$APP_NAME" "$APP_VERSION")
    echo $ARCHIVE

    rm $ARCHIVE

    cd $BUILD_RELEASE_DIR
    tar -zcvf $ARCHIVE $APP_BUNDLE $APP_BUNDLE_QMAPTOOL
    cd ..
}


if [[ "$1" == "" ]]; then
    echo "---extract version -----------------"
    extractVersion
    readRevisionHash
    echo "---build bundle --------------------"
    buildAppStructure
    extendAppStructure
    echo "---replace version string ----------"
    updateInfoPlist
    if [[ "$BREW_PACKAGE_BUILD" == "" ]] ; then
        echo "---qt deploy tool ------------------"
        qtDeploy
    fi
    echo "---copy libraries ------------------"
    copyAdditionalLibraries
    echo "---copy external files -------------"
    copyQtTrqnslations
    copyExternalFiles
    copyExternalHelpFiles_QMS
    if [ -z "$BREW_PACKAGE_BUILD"]; then
        # copy only if built as standalone package (QMS not as a brew pkg)
        echo "---adjust linking ------------------"
        adjustLinking
    fi
    echo "---external tools ------------------"
    copyExtTools
    if [ -z "$BREW_PACKAGE_BUILD" ]; then
        # copy only if built as standalone package (QMS not as a brew pkg)
        adjustLinkingExtTools
    fi
    printLinkingExtTools
    echo "------------------------------------"

    # Codesign the apps (on arm64 mandatory):
    echo "${INFO}Signing app bundles${NC}"

    # 1. remove all empty directories, otherwiese verification of signing will fail
    find $BUILD_BUNDLE_CONTENTS_DIR -type d -empty -delete

    if [ -z "$BREW_PACKAGE_BUILD" ]; then
        # copy only if built as standalone package (QMS not as a brew pkg)
    # 2. sign gdalbuild (special hack), since it is an executable copied from outside into the bundle
    # install_name_tool -add_rpath @executable_path/../Frameworks $BUILD_RELEASE_DIR/QMapShack.app/Contents/Tools/gdalbuildvrt
    # codesign -s <Apple Dev Account> --force --deep --sign - $BUILD_RELEASE_DIR/QMapShack.app/Contents/Tools/gdalbuildvrt
    codesign -s manfred.kern@gmail.com --force --deep --sign - $BUILD_RELEASE_DIR/QMapShack.app/Contents/Tools/gdalbuildvrt
    # codesign  --force --deep --sign - $BUILD_RELEASE_DIR/QMapShack.app/Contents/Tools/gdalbuildvrt
    fi

    # 3. sign the complete app bundle
    # codesign -s <Apple Dev Account> --force --deep --sign - $BUILD_RELEASE_DIR/QMapShack.app
    codesign -s manfred.kern@gmail.com --force --deep --sign - $BUILD_RELEASE_DIR/QMapShack.app
    # codesign --force --deep --sign - $BUILD_RELEASE_DIR/QMapShack.app
fi

if [[ "$1" == "archive" ]]; then
    extractVersion
    archiveBundle
fi
