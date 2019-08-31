CC=g++
CFLAGS= -Wall -Wextra -std=c++11 -g
COMMON_DEP= Utils.h SegHeader.h Timer.h Timer.cpp

default: server client
server: Errcode.h Server.cpp Server.h servermain.cpp Connection.h Connection.cpp $(COMMON_DEP)
	$(CC) $(CFLAGS) \
		Server.cpp servermain.cpp Timer.cpp Connection.cpp -o server
client: Client.cpp Client.h clientmain.cpp $(COMMON_DEP)
	$(CC) $(CFLAGS) Client.cpp clientmain.cpp Timer.cpp -o client

zip: Errcode.h Server.cpp Server.h servermain.cpp Connection.h Connection.cpp \
	Client.cpp Client.h clientmain.cpp $(COMMON_DEP) Makefile
	zip project2_404798097_104939937.zip Errcode.h Server.cpp Server.h servermain.cpp Connection.h Connection.cpp \
	Client.cpp Client.h clientmain.cpp report.pdf README $(COMMON_DEP) Makefile
