NAME=nanodbc
CC=g++
CXXFLAGS=-shared -fPIC -lstdc++ -lodbc -std=gnu++11 -DNANODBC_USE_BOOST_CONVERT
SOURCES_ORIG=src/nanodbc.cpp
SOURCES=src/Nanodbc.cpp
HEADERS_ORIG=src/nanodbc.h
HEADERS=src/Nanodbc.h
OUTPUT=libnanodbc.so

all: $(OUTPUT)

$(OUTPUT): $(SOURCES) $(HEADERS)
	$(CC) $(CXXFLAGS) -o $(OUTPUT) $(SOURCES)

clean:
	rm -f *.o *.so

install:
	mv $(OUTPUT) /usr/local/lib64
	cp $(HEADERS) /usr/include/

.PHONY:
	install clean
