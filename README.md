# Build & Run Instructions

## Prerequisites

curl -O https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.c
curl -O https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.h

curl -O https://raw.githubusercontent.com/webview/webview/master/webview.h

sudo apt install libwebkit2gtk-4.1-dev build-essential

## Compilation

To compile the core application:
```bash
gcc main.c -o config_manager `pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.1` -DWEBVIEW_GTK
