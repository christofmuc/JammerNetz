#!/bin/sh

# This is an alternative way to cpack to produce a Mac DMG image for the application. 
#
# you can install create-dmg via brew: brew install create-dmg

create-dmg --volname digitalStagePC-2.2.0 \
	--volicon build-clean/installer/digitalStagePC.app/Contents/Resources/digitalStagePC.icns	\
	--app-drop-link 20 20 \
	--eula LICENSE.md \
	--hdiutil-verbose \
	--sandbox-safe \
	digitalStagePC-2.2.0.dmg \
	build-clean/installer
