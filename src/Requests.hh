#pragma once
#include <iostream>
#include <cstring>

class ServerState;

// Do not forget to recycle Requests after handling!
class Request {
public:
  virtual ~Request() = default; // Virtual destructor for proper cleanup
  virtual void handle(ServerState &state) = 0;
  static Request *make_get_request(int client, std::string headers);
  static Request *make_post_request(int client, std::string headers, std::string body);
  
  // Helper functions
  static void reply(int client, const char *status_line, const char *body);
protected:
  int client_socket = -1;
}; 

class HealthRequest : public Request { // GET /health
public:
  void handle(ServerState &state) override;
  HealthRequest(int client)
  {
    std::cout << "DEBUG: HealthRequest constructed" << std::endl;
    this->client_socket = client;
  }
};

class TeapotRequest : public Request { // GET /teapot
public:
  void handle(ServerState &state) override;
  TeapotRequest(int client)
  {
    std::cout << "DEBUG: TeapotRequest constructed" << std::endl;
    this->client_socket = client;
  }
};

class ListRequest : public Request { // GET /jobs
public:
  void handle(ServerState &state) override;
  ListRequest(int client)
  {
    std::cout << "DEBUG: ListRequest constructed" << std::endl;
    this->client_socket = client;
  }
};

class ListScriptsRequest : public Request { // GET /scripts
public:
  void handle(ServerState &state) override;
  ListScriptsRequest(int client)
  {
    std::cout << "DEBUG: ListScriptsRequest constructed" << std::endl;
    this->client_socket = client;
  }
};

class DeleteRequest : public Request { // GET /scripts/<id>/delete
public:
  void handle(ServerState &state) override;
  DeleteRequest(int client, int id)
  {
    std::cout << "DEBUG: DeleteRequest constructed" << std::endl;
    this->client_socket = client;
    this->id = id;
  }
  int id;
};

class JobStatusRequest : public Request { // GET /jobs/<id>
public:
  void handle(ServerState &state) override;
  JobStatusRequest(int client, int id)
  {
    std::cout << "DEBUG: JobStatusRequest constructed" << std::endl;
    this->client_socket = client;
    this->id = id;
  }
  int id;
}; 

class TerminateRequest : public Request { // GET /jobs/<id>/terminate
public:
  void handle(ServerState &state) override;
  TerminateRequest(int client, int id)
  {
    std::cout << "DEBUG: TerminateRequest constructed" << std::endl;
    this->client_socket = client;
    this->id = id;
  }
  int id;
};

class StdoutRequest : public Request { // GET /jobs/<id>/stdout
public:
  void handle(ServerState &state) override;
  StdoutRequest(int client, int id)
  {
    std::cout << "DEBUG: StdoutRequest constructed" << std::endl;
    this->client_socket = client;
    this->id = id;
  }
  int id;
}; 

class StderrRequest : public Request { // GET /jobs/<id>/stderr
public:
  void handle(ServerState &state) override;
  StderrRequest(int client, int id)
  {
    std::cout << "DEBUG: StderrRequest constructed" << std::endl;
    this->client_socket = client;
    this->id = id;
  }
  int id;
}; 

class RunRequest : public Request { // POST /scripts/<id>/run + args
public:
  void handle(ServerState &state) override;
  RunRequest(int client, std::string headers, std::string body);
  int id;
  std::string args;
};

class UploadRequest : public Request { // POST /scripts/upload
public:
  void handle(ServerState &state) override;
  UploadRequest(int client, std::string headers, std::string body);
  std::string script;
  std::string filename;
};

using Task = Request;
using TaskHealth = HealthRequest;
using TaskTeapot = TeapotRequest;
using TaskGetJobs = ListRequest;
using TaskGetScripts = ListScriptsRequest;
using TaskDeleteScript = DeleteRequest;
using TaskGetJobStatus = JobStatusRequest;
using TaskTerminateJob = TerminateRequest;
using TaskGetStdout = StdoutRequest;
using TaskGetStderr = StderrRequest;
using TaskRunScript = RunRequest;
using TaskUploadScript = UploadRequest;

