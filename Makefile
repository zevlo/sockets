CC      = cc
CFLAGS  = -Wall -Wextra -std=gnu11

DAYTIME = basic/server basic/client
CHAT    = chat/chat_server chat/chat_client
WEB     = webserver/webserver

all: $(DAYTIME) $(CHAT) $(WEB)

basic/server: basic/server.c
	$(CC) $(CFLAGS) -o $@ $<

basic/client: basic/client.c
	$(CC) $(CFLAGS) -o $@ $<

chat/chat_server: chat/chat_server.c
	$(CC) $(CFLAGS) -o $@ $< -pthread

chat/chat_client: chat/chat_client.c
	$(CC) $(CFLAGS) -o $@ $< -pthread

webserver/webserver: webserver/webserver.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(DAYTIME) $(CHAT) $(WEB)

.PHONY: all clean
