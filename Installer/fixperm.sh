#!/bin/sh

# PackageMaker is horrible when it comes down to handling of permissions.
# When it creates new directories their permissions are pretty much random.
# I tried running this as a postinstall script but it did zilch.
# Postflight seems to work. This script is executed as root.
chown root:admin /Library/Application\ Support/CM6206
chmod a+rx /Library/Application\ Support/CM6206
