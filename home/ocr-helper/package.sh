#!/usr/bin/env bash
# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Makes the OCR helper's package: Furoshiki-ocr-helper.zip, with the folder ocr-helper-setup in
# it (the code with its three parts, the requirements, the installer, this folder's README and
# the licence), to be unzipped into Furoshiki's folder and installed with install.cmd.
#   ocr-helper/package.sh [OUT-FOLDER]      (default: ocr-helper/build)
# The three parts and the requirements are beside the code in a published tree; in the
# repository they come from they are taken from where they are kept (furochibi/).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${1:-$here/build}"
parts="$here/furoshiki_ocr"
requirements="$here/requirements"
if [ ! -f "$parts/engine.py" ]; then
  parts="$here/../../furochibi/server/console_ocr"
  requirements="$here/../../furochibi/requirements"
fi
for needed in "$parts/engine.py" "$parts/stabiliser.py" "$parts/cleaner.py" "$requirements/runtime.txt" "$requirements/helper-windows.txt"; do
  [ -f "$needed" ] || { echo "package.sh: $needed is missing" >&2; exit 1; }
done
stage="$out/ocr-helper-setup"
rm -rf "$stage" "$out/Furoshiki-ocr-helper.zip"
mkdir -p "$stage/furoshiki_ocr" "$stage/requirements"
cp "$here"/furoshiki_ocr/*.py "$stage/furoshiki_ocr/"
cp "$parts/engine.py" "$parts/stabiliser.py" "$parts/cleaner.py" "$stage/furoshiki_ocr/"
cp "$requirements/runtime.txt" "$requirements/helper-windows.txt" "$stage/requirements/"
cp "$here/install.ps1" "$here/install.cmd" "$here/README.md" "$stage/"
cp "$here/../COPYING" "$stage/COPYING.txt"
git -C "$here" rev-parse --short HEAD > "$stage/BUILD" 2>/dev/null || echo unknown > "$stage/BUILD"
# The package must hold nothing but itself: its code loads with only what is in it.
( cd "$stage" && python3 -I -c "
import ast, pathlib, sys
for file in pathlib.Path('furoshiki_ocr').glob('*.py'):
    ast.parse(file.read_text(encoding='utf-8'))
text = pathlib.Path('furoshiki_ocr/engine.py').read_text(encoding='utf-8')
sys.exit(0 if 'ENGINES' in text else 'engine.py is not the engines')" )
( cd "$out" && python3 -I -c "
import pathlib, zipfile
with zipfile.ZipFile('Furoshiki-ocr-helper.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for file in sorted(pathlib.Path('ocr-helper-setup').rglob('*')):
        if file.is_file():
            z.write(file)
print('Furoshiki-ocr-helper.zip:', len(z.namelist()), 'files')" )
ls -la "$out/Furoshiki-ocr-helper.zip"
