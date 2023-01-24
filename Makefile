build: master nave porto

run: master nave porto
	./bin/master

master: master.c
	gcc -std=c89 -o bin/master master.c lib/config.c lib/utilities.c

nave: nave.c
	gcc -std=c89 -o bin/nave nave.c lib/config.c lib/utilities.c

porto: porto.c
	gcc -std=c89 -o bin/porto porto.c lib/config.c lib/utilities.c