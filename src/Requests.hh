#pragma once
#include <iostream>
#include <cstring>

// Do not forget to recycle Requests after handling!
class Request {
public:
  virtual ~Request() = default; // Virtual destructor for proper cleanup
  static Request *make_get_request(int client, std::string headers);
  static Request *make_post_request(int client, std::string headers, std::string body);
  
  // Helper functions
  static void reply(int client, const char *status_line, const char *body);
protected:
  int client_socket = -1;
}; 

class HealthRequest : public Request { // GET /health
public:
  HealthRequest(int client)
  {
    std::cout << "DEBUG: HealthRequest constructed" << std::endl;
    this->client_socket = client;
  }
};

class TeapotRequest : public Request { // GET /teapot
public:
  TeapotRequest(int client)
  {
    std::cout << "DEBUG: TeapotRequest constructed" << std::endl;
    this->client_socket = client;
  }
};

class ListRequest : public Request { // GET /jobs
public:
  ListRequest(int client)
  {
    std::cout << "DEBUG: ListRequest constructed" << std::endl;
    this->client_socket = client;
  }
};

class ListScriptsRequest : public Request { // GET /scripts
public:
  ListScriptsRequest(int client)
  {
    std::cout << "DEBUG: ListScriptsRequest constructed" << std::endl;
    this->client_socket = client;
  }
};

class DeleteRequest : public Request { // GET /scripts/<id>/delete
public:
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
  RunRequest(int client, std::string headers, std::string body);
  int id;
  std::string args;
};

class UploadRequest : public Request { // POST /scripts/upload
public:
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

