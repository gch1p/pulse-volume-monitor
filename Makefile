CC=g++
LIBS=libpulse dbus-1
CFLAGS=-std=c++11 -Wall -pedantic -Wextra `pkg-config --cflags ${LIBS}`
LDFLAGS=`pkg-config --libs ${LIBS}`

all: main

main: main.o
	${CC} main.o -o pulse-volume-monitor ${LDFLAGS}

main.o:
	${CC} ${CFLAGS} -c main.cpp
