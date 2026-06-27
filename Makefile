CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -Wno-deprecated-declarations
TARGET = mixtape
TEST_TARGET = test_runner

WEBVIEW_FLAGS = `pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.1` -ldl

.PHONY: all test run clean

all: $(TARGET)

$(TARGET): main.c server.h
	$(CC) $(CFLAGS) main.c -o $(TARGET) $(WEBVIEW_FLAGS)

test: test_suite.c server.h
	$(CC) $(CFLAGS) test_suite.c -o $(TEST_TARGET)
	./$(TEST_TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET) cfg.yml
