wlines.exe: wlines.c
	$(CC) -DWLINES_VERSION='"$(shell git log -1 --date=short "--format=%cd-%h")"' -Wall -Werror -Wextra -std=c99 -pedantic -s -O2 $^ -o $@ -static -lgdi32 -luser32 -lshlwapi

.PHONY: clean
clean:
	rm -f wlines.exe

