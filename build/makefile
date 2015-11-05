
PEER_TARGET = peer
COMMON_INC = -I ../common

PEER_SOURCE := $(wildcard ../peer/*.cpp) $(wildcard ../common/*.cpp)
PEER_OBJS := $(patsubst %.cpp, %.o, $(PEER_SOURCE))

CXXFLAGS := -g -Wall -DDEBUG $(COMMON_INC)

all:$(PEER_TARGET)

$(PEER_TARGET): $(PEER_OBJS)
	g++ -o $(PEER_TARGET) $(PEER_OBJS) -l pthread -lghthash
	cp $(PEER_TARGET) ./peer0/$(PEER_TARGET)
	cp $(PEER_TARGET) ./peer1/$(PEER_TARGET)
	cp $(PEER_TARGET) ./peer2/$(PEER_TARGET)
	cp $(PEER_TARGET) ./peer3/$(PEER_TARGET)
	cp $(PEER_TARGET) ./peer4/$(PEER_TARGET)
	cp $(PEER_TARGET) ./peer5/$(PEER_TARGET)
	cp $(PEER_TARGET) ./peer6/$(PEER_TARGET)
	cp $(PEER_TARGET) ./peer7/$(PEER_TARGET)

clean:
	rm -rf $(PEER_OBJS) $(PEER_TARGET) 

