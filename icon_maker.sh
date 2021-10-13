#!/bin/zsh

export ICON_DIR=build/new_icon.iconset
export ORIGINAL_ICON=lookAndFeel/Resources/DigitalStageIcon_512.png

rm -rf $ICON_DIR
mkdir $ICON_DIR

# Normal screen icons
for SIZE in 16 32 64 128 256 512; do
sips -z $SIZE $SIZE $ORIGINAL_ICON --out $ICON_DIR/icon_${SIZE}x${SIZE}.png ;
done

# Retina screen icons
for SIZE in 32 64 256 512; do
sips -z $SIZE $SIZE $ORIGINAL_ICON --out $ICON_DIR/icon_$(expr $SIZE / 2)x$(expr $SIZE / 2)x2.png ;
done

iconutil -c icns -o lookAndFeel/Resources/digitalStagePC.icns $ICON_DIR
