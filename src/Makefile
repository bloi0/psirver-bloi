CXXFLAGS = -std=c++11 -Wall -Wextra -Wpedantic -g -fPIC -fstack-protector-all 
OBJS = psirver.o Requests.o # More object files can be placed on this line
BIN = ./psirver # The main binary

SERVER = http://localhost:8000
CURLFLAGS = -s -o /dev/null -w "%{http_code}\\n"

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

test:
	@curl -F "file=@Makefile" $(CURLFLAGS) $(SERVER)/health # POST a file
	@curl -d "args=val1,val2" $(CURLFLAGS) $(SERVER)/health # POST parameters
	@curl                     $(CURLFLAGS) $(SERVER)/health # GET
