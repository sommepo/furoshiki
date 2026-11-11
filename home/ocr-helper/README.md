# Furoshiki's OCR helper

Reads the text of a game's window for Furoshiki, on your own computer: Furoshiki sends it the
window's picture four times a second, and it answers with each new line of Japanese text and
where every character stands.

It is a small Python service (`furoshiki_ocr`) around three parts: the OCR engines
([owocr](https://github.com/AuroraWright/owocr)'s, among them
[meikiocr](https://github.com/rtr46/meikiocr)), a stabiliser that waits until the text has
stopped changing before it reads, and a cleaner that drops furigana, cursors and repeats and
puts a line that was still being written in the place of its beginning.

## Installing (Windows 10 or 11, 64-bit)

1. Unzip `Furoshiki-ocr-helper.zip` into Furoshiki's own folder.
2. Run `install.cmd` in the unzipped folder. It needs the internet once (about 1 GB: Python,
   the packages and meikiocr's model) and no administrator.

Everything goes into `FuroshikiOcrHelper` in Furoshiki's folder and nowhere else. Nothing runs
until Furoshiki starts it: *Add a title*, *B, a game window*. To remove it, run
`install.cmd -Uninstall`, or delete that folder.

## Engines

Chosen in Furoshiki: *Settings, Sources, OCR engine*.

| Engine | Where it reads | Notes |
| --- | --- | --- |
| meikiocr | This computer | The default. Made for game text. |
| OneOCR | This computer | Windows 11's own (the Snipping Tool's). |
| Chrome Screen AI | This computer | Fetches its library the first time it is chosen. |
| Google Lens | Google | **Sends each text area it reads to Google.** |

## Text boxes

A game can have several boxes, drawn in *Add a title*: the text's own; others read with it as
one line (a name above the message); and boxes read only when you ask (a menu, a status
screen), with Ctrl+Shift+R, or R on Furoshiki's text strip.

## What it answers, and to whom

Only this computer (it listens on 127.0.0.1 and refuses anything passed on by a proxy), and
only who says the token its installer made. `furoshiki_ocr/app.py` lists its requests.

## From source

`python -m furoshiki_ocr --config config.json` with the packages of `requirements/`;
`python -m unittest discover -s tests` runs its tests. `package.sh` makes the zip.
