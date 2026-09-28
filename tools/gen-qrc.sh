#!/usr/bin/env sh
# Regenerate src/resources.qrc from the files on disk. Run after adding a QML file.
set -eu
cd "$(dirname -- "$0")/../src"
{
  echo '<RCC>'
  echo '    <qresource prefix="/">'
  for f in qml/*.qml ui/Icon.qml ui/icons/Icons.qml; do echo "        <file>$f</file>"; done
  echo '    </qresource>'
  echo '    <!-- The control style, where Qt looks for styles: qrc:/qt/qml/<name>. -->'
  echo '    <qresource prefix="/qt/qml/OmaScanStyle">'
  for f in style/OmaScanStyle/*; do echo "        <file alias=\"$(basename "$f")\">$f</file>"; done
  echo '    </qresource>'
  echo '</RCC>'
} > resources.qrc
echo "wrote src/resources.qrc"
