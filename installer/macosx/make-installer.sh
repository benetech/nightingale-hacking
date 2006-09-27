#!/bin/sh

DEPTH=../../..
CURRENT_DIR=`pwd`
CURRENT_DATE=`date +%Y%m%d`
ARCH="$1"

rm -rf ${DEPTH}/_built_installer
mkdir ${DEPTH}/_built_installer

cp ${DEPTH}/installer/macosx/LICENSE.txt ${DEPTH}/compiled/dist/Songbird.app/
cp ${DEPTH}/installer/macosx/TRADEMARK.txt ${DEPTH}/compiled/dist/Songbird.app/

${DEPTH}/installer/macosx/make-diskimage ${DEPTH}/_built_installer/Songbird_${CURRENT_DATE}_${ARCH}.dmg ${DEPTH}/compiled/dist Songbird -null- ${DEPTH}/installer/macosx/songbird.dsstore
