CXXFLAGS = -std=c++11 -Wall -Wextra -Wpedantic -g -fPIC

OBJS = test.o # More object files can be placed on this line

BIN = ./psirver # The main binary

all: $(BIN)

include DEPEND

$(BIN): $(OBJS) 
	$(CXX) -o $(BIN) $(OBJS) $(LDLAGS)

.cc.o:
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f $(BIN) $(OBJS) DEPEND

DEPEND: 
	$(CXX) -MM *.cc > DEPEND

