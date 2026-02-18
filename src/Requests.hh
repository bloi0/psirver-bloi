#pragma once
#include <iostream>
#include <cstring>

// Do not forget to recycle Requests after execution!
class Request {
public:
  virtual ~Request() = default; // Virtual destructor for proper cleanup
  virtual int execute() = 0;
  static Request *make_get_request(int client, std::string headers);
  static Request *make_post_request(int client, std::string headers, std::string body);
  
  // Helper functions
  static void reply(int client, const char *status_line, const char *body);
  static void reply_with_location(int client, const char *status_line, const char *location, const char *body);
protected:
  int client_socket = -1; // Store for execute() methods
}; 

class HealthRequest : public Request { // GET /health
public:
  HealthRequest(int client) { this->client_socket = client; }
  int execute();
};

class TeapotRequest : public Request { // GET /teapot
public:
  TeapotRequest(int client) { this->client_socket = client; }
  int execute();
};

class ListRequest : public Request { // GET /jobs
public:
  ListRequest(int client) { this->client_socket = client; }
  int execute();
};

class ListScriptsRequest : public Request { // GET /scripts
public:
  ListScriptsRequest(int client) { this->client_socket = client; }
  int execute();
};

class DeleteRequest : public Request { // GET /scripts/<id>/delete
public:
  DeleteRequest(int client, int id) { this->client_socket = client; this->id = id; }
  int execute();
  int id;
};

class JobStatusRequest : public Request { // GET /jobs/<id>
public:
  JobStatusRequest(int client, int id) { this->client_socket = client; this->id = id; }
  int execute();
  int id;
}; 

class TerminateRequest : public Request { // GET /jobs/<id>/terminate
public:
  TerminateRequest(int client, int id) { this->client_socket = client; this->id = id; }
  int execute();
  int id;
};

class StdoutRequest : public Request { // GET /jobs/<id>/stdout
public:
  StdoutRequest(int client, int id) { this->client_socket = client; this->id = id; }
  int execute();
  int id;
}; 

class StderrRequest : public Request { // GET /jobs/<id>/stderr
public:
  StderrRequest(int client, int id) { this->client_socket = client; this->id = id; }
  int execute();
  int id;
}; 

class RunRequest : public Request { // POST /scripts/<id>/run + args
public:
  RunRequest(int client, std::string headers, std::string body);
  int execute();
  int id;
  std::string args;
};

class UploadRequest : public Request { // POST /scripts/upload
public:
  UploadRequest(int client, std::string headers, std::string body);
  std::string script;
  std::string filename;
  int execute();
};

