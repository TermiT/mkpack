CXX=g++
CFLAGS=-IPVRTexLib -DMKPAK
LIBS=-LPVRTexLib/Linux
CFLAGS+=$(shell pkg-config --cflags sndfile)
LIBS+=$(shell pkg-config --libs sndfile)

all: mkpak

mkpak: mkpak.cpp
	$(CXX) $(CFLAGS) $(LIBS) -o $@ $^ -lPVRTexLib
